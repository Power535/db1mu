/*
 * Copyright (c) 2015, <copyright holder> <email>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *     * Neither the name of the <organization> nor the
 *     names of its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY <copyright holder> <email> ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL <copyright holder> <email> BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "b1mainwindow.h"
#include "ui_b1mainwindow.h"
#include "glbe.h"

#include <QCoreApplication>
#include <QMessageBox>
#include <QCloseEvent>
#include <QFileDialog>
#include <iostream>

#include <bus.h>
#include <cpu6502.h>
#include <PPU.h>
#include <Cartridge.h>
#include <loader.h>
#include <log.h>

struct NESEngine
{
     Bus bus;
     CPU6502 cpu;
     PPU ppu;
     Cartrige cartridge;
     bool ready = false;

     NESEngine(OutputMode mode, PPU::RenderingBackend *pBackend):
        bus { mode },
        cpu { bus },
        ppu { bus, pBackend }
    {
        bus.setCPU(&cpu);
        bus.setPPU(&ppu);
    }
};

b1MainWindow::b1MainWindow()
{
    m_ui = new Ui::b1MainWindow;
    m_ui->setupUi ( this );

    m_screen = new ScreenWidget { this };
    setCentralWidget(m_screen);

    auto &logCfg = Log::instance().config();
    logCfg.pOutput = &std::cout;
    logCfg.filter = Log::LEVEL_DEBUG;
    logCfg.autoFlush = true;

    m_eng.reset(new NESEngine { OutputMode::NTSC,
                                m_screen->getRenderingBackend() });
    m_screen->setBus(&m_eng->bus);

    connect(m_screen, SIGNAL(fpsChanged(float)), SLOT(fpsUpdated(float)));

    m_cpuState = new CPUStateDialog { this };
    QObject::connect(m_ui->actionShowCPU, SIGNAL(toggled(bool)),
                     m_cpuState, SLOT(setVisible(bool)));
    QObject::connect(m_cpuState, &CPUStateDialog::finished,
                     [this](int) { m_ui->actionShowCPU->setChecked(false); });
    m_ppuState = new PPUStateDialog { this };
    QObject::connect(m_ui->actionShowPPU, SIGNAL(toggled(bool)),
                     m_ppuState, SLOT(setVisible(bool)));
    QObject::connect(m_ppuState, &PPUStateDialog::finished,
                     [this](int) { m_ui->actionShowPPU->setChecked(false); });
}

b1MainWindow::~b1MainWindow()
{
    delete m_ui;
}

void b1MainWindow::closeEvent(QCloseEvent *e)
{
    const auto r = QMessageBox::question(this,
                                         tr("Confirm exit"),
                                         tr("Are you sure want to quit?"),
                                         QMessageBox::Yes | QMessageBox::No);
    if (r == QMessageBox::Yes)
    {
        e->accept();
        //foo();
    }
    else
    {
        e->ignore();
    }
}

void b1MainWindow::openROM()
{
    const auto fn = QFileDialog::getOpenFileName(this,
                                                 tr("Select ROM file"),
                                                 tr("."),
                                                 tr("NES ROM images (*.nes)"));
    if (!fn.isNull())
    {
        if (m_screen->isRunning())
            m_screen->pause();

        ROMLoader loader { m_eng->cartridge };
        try
        {
            loader.loadNES(fn.toLocal8Bit().data());
            m_eng->bus.injectCartrige(&m_eng->cartridge);
            m_screen->resume();
        }
        catch (const Exception &ex)
        {
            QMessageBox::critical(this,
                                  tr("Cannot load ROM"),
                                  tr("Error: %1").arg(ex.message()));
        }
        updateUI();
    }
}

void b1MainWindow::pauseEmulation()
{
    m_screen->pause();

    m_cpuState->show(&m_eng->cpu);
    m_ppuState->show(&m_eng->ppu);
    updateUI();
}

void b1MainWindow::resumeEmulation()
{
    m_screen->resume();

    m_cpuState->clear();
    m_ppuState->clear();
    updateUI();
}

void b1MainWindow::stepEmulation()
{
    m_screen->step();

    m_cpuState->show(&m_eng->cpu);
    m_ppuState->show(&m_eng->ppu);
}

void b1MainWindow::updateUI()
{
    const bool r = m_screen->isRunning();
    m_ui->actionPause->setEnabled(r);
    m_ui->actionResume->setEnabled(!r);
    m_ui->actionStep->setEnabled(!r);
}

void b1MainWindow::fpsUpdated(float fps)
{
    statusBar()->showMessage(tr("%1 FPS").arg(fps, 5, 'f', 0));
}
