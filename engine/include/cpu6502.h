#ifndef CPU6502_H
#define CPU6502_H

#include "common.h"
#include "bus.h"
#include <tuple>
#include <array>
#include <type_traits>

#ifdef ENABLE_CPU_TRACE
#include "log.h"
#define TRACE(...) Log::v(__VA_ARGS__)
#else
#define TRACE(...)
#endif

class CPU6502
{
    friend class Debugger;

public:
    enum State
    {
        STATE_HALTED,
        STATE_RUN,
        STATE_ERROR
    };

    CPU6502(Bus &bus);

    int run(int clk) noexcept;

    void reset();
    int IRQ();
    int NMI();

    State state() const noexcept
    {
        return m_state;
    }

private:
    /* Layout:
     * - accumulator
     * - flags
     * - X, Y indexes
     * - stack pointer
     * - program counter
     */
    struct Reg
    {
        c6502_byte_t a, x, y, s, p;
        c6502_word_t pc;
    } m_regs;

    State m_state;
    Bus &m_bus;
    int m_penalty;

    using OpHandler = void (CPU6502::*)(void);
    using OpData = std::tuple<OpHandler, int, bool>;
    static constexpr int OPCODE_COUNT = 0xFF;

    static std::array<OpData, OPCODE_COUNT> s_opHandlers;

    enum class Flag: c6502_byte_t
    {
        C = 0, Z = 1, I = 2, D = 3, B = 4, V = 6, N = 7
    };

    template <Flag FLG>
    c6502_byte_t getFlag() const noexcept
    {
        constexpr c6502_byte_t off = static_cast<c6502_byte_t>(FLG);
        return (m_regs.p & (1u << off)) >> off;
    }

    template <Flag FLG>
    void setFlag(c6502_byte_t x) noexcept
    {
        assert(x < 2u);
        constexpr c6502_byte_t off = static_cast<c6502_byte_t>(FLG);
        m_regs.p = (m_regs.p & ~(1u << off)) | ((x & 1u) << off);
    }

    c6502_byte_t readMem(c6502_word_t addr) noexcept
    {
        return m_bus.readMem(addr);
    }

    void writeMem(c6502_word_t addr, c6502_byte_t val) noexcept
    {
        m_bus.writeMem(addr, val);
    }

    int step();

    // Helpers
    // Push to / pop from the stack shorthands
    void push(c6502_byte_t v) noexcept
    {
        assert(m_regs.s > 0u && "Stack overflow");
        writeMem(0x100u | static_cast<c6502_word_t>(m_regs.s--), v);
    }

    c6502_byte_t pop() noexcept
    {
        assert(m_regs.s < 0xFFu && "Stack underflow");
        return readMem(0x100u | static_cast<c6502_word_t>(++m_regs.s));
    }

    // Get the byte PC points to and increase PC by 1
    c6502_byte_t advance() noexcept
    {
        return readMem(m_regs.pc++);
    }

    /// Addressing modes
    enum class AM
    {
        ACC, IMM, ZP, ZP_X, ZP_Y, ABS, ABS_X, ABS_Y, IND, IND_X, IND_Y, DEF
    };

    template <AM M>
    c6502_word_t fetchAddr() noexcept
    {
        // Compiler shouldn't normally instantiate this generic function body
        static_assert(M != M, "Unsupported addressing mode");
        return 0;
    }

    template <AM M>
    c6502_byte_t fetchOperand() noexcept
    {
        const auto addr = fetchAddr<M>();
        const auto eo = readMem(addr);
        TRACE("Operand value = %X", eo);

        return eo;
    }

    template <Flag F, bool IS_SET>
    void branchIf() noexcept
    {
        constexpr c6502_byte_t n = IS_SET ? 0 : 1;
        const auto rdis = fetchOperand<AM::IMM>();
        if (getFlag<F>() ^ n)
        {
            m_penalty = 1;
            const c6502_byte_t oldPC_h = hi_byte(m_regs.pc - 1);
            if (rdis & 0x80u)
                m_regs.pc -= 0x100u - rdis;
            else
                m_regs.pc += rdis;
            TRACE("Branch to %X", m_regs.pc);
            if (oldPC_h != hi_byte(m_regs.pc))
                m_penalty = 2;
        }
    }

    template <typename T>
    void eval_C(const T r) noexcept
    {
        static_assert(sizeof(T) > 1 && std::is_unsigned<T>::value,
                      "incorrect argument type (must be unsigned and at least 2 bytes long)");

        setFlag<Flag::C>(r > 0xFFu ? 1u : 0u);
    }

    void eval_Z(const c6502_byte_t r) noexcept
    {
        setFlag<Flag::Z>(r == 0u ? 1u : 0u);
    }

    void eval_N(const c6502_byte_t r) noexcept
    {
        setFlag<Flag::N>((r >> 7u) & 1u);
    }

    // 6502 commands
    #define CMD_DECL(name) \
    template <AM MODE> \
    void cmd_##name() noexcept;

    CMD_DECL(ADC)
    CMD_DECL(AND)
    CMD_DECL(ASL)
    CMD_DECL(BCC)
    CMD_DECL(BCS)
    CMD_DECL(BEQ)
    CMD_DECL(BIT)
    CMD_DECL(BMI)
    CMD_DECL(BNE)
    CMD_DECL(BPL)
    CMD_DECL(BRK)
    CMD_DECL(BVC)
    CMD_DECL(BVS)
    CMD_DECL(CLC)
    CMD_DECL(CLD)
    CMD_DECL(CLI)
    CMD_DECL(CLV)
    CMD_DECL(CMP)
    CMD_DECL(CPX)
    CMD_DECL(CPY)
    CMD_DECL(DEC)
    CMD_DECL(DEX)
    CMD_DECL(DEY)
    CMD_DECL(EOR)
    CMD_DECL(INC)
    CMD_DECL(INX)
    CMD_DECL(INY)
    CMD_DECL(JMP)
    CMD_DECL(JSR)
    CMD_DECL(LDA)
    CMD_DECL(LDX)
    CMD_DECL(LDY)
    CMD_DECL(LSR)
    CMD_DECL(NOP)
    CMD_DECL(ORA)
    CMD_DECL(PHA)
    CMD_DECL(PHP)
    CMD_DECL(PLA)
    CMD_DECL(PLP)
    CMD_DECL(ROL)
    CMD_DECL(ROR)
    CMD_DECL(RTI)
    CMD_DECL(RTS)
    CMD_DECL(SBC)
    CMD_DECL(SEC)
    CMD_DECL(SED)
    CMD_DECL(SEI)
    CMD_DECL(STA)
    CMD_DECL(STX)
    CMD_DECL(STY)
    CMD_DECL(TAX)
    CMD_DECL(TAY)
    CMD_DECL(TSX)
    CMD_DECL(TXA)
    CMD_DECL(TXS)
    CMD_DECL(TYA)
    
    #undef CMD_DECL

    static void initOpHandlers() noexcept;
};

#endif
