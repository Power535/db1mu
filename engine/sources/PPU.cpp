#include "PPU.h"
#include "log.h"

template <unsigned POS>
constexpr c6502_byte_t bit() noexcept
{
    return 1u << POS;
}

template <unsigned POS>
constexpr bool test(c6502_byte_t v) noexcept
{
    return (v & (1u << POS)) != 0;
}

c6502_byte_t PPU::readRegister(c6502_word_t n) noexcept
{
    Log::v("Reading PPU register #%d", n);

    c6502_byte_t rv = 0;
    switch (n)
    {
        case STATE:
            if (!m_enableWrite)
                rv |= bit<4>();
            if (m_spritesOnLine > 8)
                rv |= bit<5>();
            if (m_sprite0)
                rv |= bit<6>();
            if (m_vblank)
            {
                rv |= bit<7>();
                m_vblank = false;
            }
            break;
        case SPRMEM_DATA:
            rv = m_bus.readSpriteMem(m_sprmemAddr++);
            break;
        case VIDMEM_DATA:
            rv = m_bus.readVideoMem(m_vramAddr);
            if (!m_vramReadError)
                m_vramAddr += m_addrIncr;
            else
                m_vramReadError = false;
            break;
        default:
            assert(false && "Illegal PPU register for reading");
    }

    return rv;
}

void PPU::writeRegister(c6502_word_t n, c6502_byte_t val) noexcept
{
    Log::v("Writing value %d to PPU register #%d", val, n);
    switch (n)
    {
        case CONTROL1:
            switch (val & 0b11u)
            {
                case 0b00u:
                    m_activePage = 0x2000u;
                    break;
                case 0b01u:
                    m_activePage = 0x2400u;
                    break;
                case 0b10u:
                    m_activePage = 0x2800u;
                    break;
                case 0b11u:
                    m_activePage = 0x2C00u;
            }
            m_addrIncr = test<2>(val) ? 32u : 1u;
            m_baSprites = test<3>(val) ? 0x1000u : 0;
            m_baBkgnd = test<4>(val) ? 0x1000u : 0;
            m_bigSprites = test<5>(val);
            m_enableNMI = test<7>(val);
            break;
        case CONTROL2:
            m_fullBacgroundVisible = test<1>(val);
            m_allSpritesVisible = test<2>(val);
            m_backgroundVisible = test<3>(val);
            m_spritesVisible = test<4>(val);
            break;
        case SPRMEM_ADDR:
            m_sprmemAddr = val;
            break;
        case SPRMEM_DATA:
            m_bus.writeSpriteMem(m_sprmemAddr++, val);
            break;
        case VIDMEM_ADDR:
            m_vramAddr <<= 8;
            m_vramAddr = (m_vramAddr & 0xFF00u) | (val & 0xFFu);

            // Read error doesn't happen during palette access
            m_vramReadError = m_vramAddr < 0x3F00u || m_vramAddr >= 0x3F20u;
            break;
        case VIDMEM_DATA:
            m_bus.writeVideoMem(m_vramAddr, val);
            m_vramAddr += m_addrIncr;
            break;
        case SCROLL:
            if (m_currScrollReg ^= 1)
                m_scrollV = val;
            else
                m_scrollH = val;
            break;
        default:
            assert(false && "Illegal PPU register for writing");
    }
}

void PPU::update() noexcept
{
    m_vblank = false;
    buildImage();
    m_vblank = true;

    if (m_enableNMI)
        m_bus.generateNMI();
}

void PPU::buildImage() noexcept
{
    static const c6502_word_t SCROLL_LAYOUT[2][2] = {
        { 0x2800u, 0x2C00u },
        { 0x2000u, 0x2400u }
    };

    const auto mode = m_bus.getMode();

    c6502_byte_t sym[64];

    auto expandSymbol = [this, &sym](c6502_byte_t clrHi,
                                     const c6502_word_t palAddr) mutable
    {
        // Combine color values
        clrHi <<= 2;
        for (auto &pt: sym)
            if (pt > 0)
                pt = m_bus.readVideoMem(palAddr + (pt | clrHi)) | 0b11000000u;
    };

    m_pBackend->setBackground(m_bus.readVideoMem(0x3F00u));

    if (m_backgroundVisible)
    {
        // Render background image
        // Palette: 0x3F00
        const int t = m_scrollV,
                  l = m_scrollH;
        constexpr int ppr = 256,
                      ppc = 240;
        const bool skipTopAndBottom = mode == OutputMode::NTSC;
        for (int r = 0; r < 30; r++)
        {
            if (skipTopAndBottom && (r == 0 || r == 29))
                continue;

            const int y = r * 8,
                      sy = y + t;
            for (int c = 0; c < 32; c++)
            {
                const int x = c * 8,
                          sx = x + l;
                const auto pageAddr = t + l == 0 ?
                                      m_activePage :
                                      SCROLL_LAYOUT[sy / ppc][sx / ppr];

                const auto psx = sx % ppr,                   // page x coordinate
                           psy = sy % ppc,                   // page y coordinate
                           indc = (psy / 8) * 32 + psx / 8,  // index in character area
                           inda = (psy / 32) * 8 + psx / 32; // index in attributes area

                // Read color information from character area
                const auto charNum = m_bus.readVideoMem(pageAddr + indc);
                readCharacter(charNum, sym, m_baBkgnd, false, false);

                // Read color information from attribute area
                const auto clrGrp = m_bus.readVideoMem(pageAddr + 960 + inda);
                const auto offInGrp = y / 16 % 2 * 2 + x / 16 % 2;
                const c6502_byte_t clrHi = (clrGrp >> (offInGrp << 1)) & 0b11u;

                expandSymbol(clrHi, 0x3F00u);

                // Load character / attribute data
                m_pBackend->setSymbol(RenderingBackend::Layer::BACKGROUND,
                                      x - l % 8, y - t % 8,
                                      sym);
            }
        }
    }

    m_sprite0 = false;
    if (m_spritesVisible)
    {
        // TODO: handle 8x16 sprites
        assert(!m_bigSprites);

        for (c6502_word_t ns = 0; ns < 64u; ns++)
        {
            const auto i = (63u - ns) * 4u;
            const auto y = m_bus.readSpriteMem(i),
                       nChar = m_bus.readSpriteMem(i + 1),
                       attrs = m_bus.readSpriteMem(i + 2),
                       x = m_bus.readSpriteMem(i + 3);
            const auto lyr = test<5>(attrs) ?
                             RenderingBackend::Layer::BEHIND :
                             RenderingBackend::Layer::FRONT;
            const c6502_byte_t clrHi = attrs & 0b11u;

            readCharacter(nChar, sym, m_baSprites, test<6>(attrs), test<7>(attrs));

            expandSymbol(clrHi, 0x3F10u);

            // Read symbol, parse attributes
            m_pBackend->setSymbol(lyr, x, y, sym);

            m_sprite0 = ns == 0;
        }
    }

    m_pBackend->draw();
}

void PPU::readCharacter(c6502_word_t ind,
                        c6502_byte_t (&sym)[64],
                        const c6502_word_t baseAddr,
                        const bool fliph,
                        const bool flipv) noexcept
{
    const auto ba = baseAddr + ind * 16;
    for (c6502_word_t i = 0; i < 8; i++)
    {
        const auto r0 = m_bus.readVideoMem(ba + i),
                   r1 = m_bus.readVideoMem(ba + i + 8);
        const auto off = (flipv ? 7 - i : i) * 8;
        for (c6502_word_t j = 0; j < 8; j++)
        {
            auto &d = sym[off + (fliph ? j : 7 - j)];
            d = (((r1 >> j) & 1u) << 1) | ((r0 >> j) & 1u);
        }
    }
}
