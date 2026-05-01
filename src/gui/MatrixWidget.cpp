/*
 * MidiEditor
 * Copyright (C) 2010  Markus Schwenk
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "MatrixWidget.h"
#include "../MidiEvent/MidiEvent.h"
#include "../MidiEvent/NoteOnEvent.h"
#include "../MidiEvent/OffEvent.h"
#include "../MidiEvent/TempoChangeEvent.h"
#include "../MidiEvent/TimeSignatureEvent.h"
#include "../midi/MidiChannel.h"
#include "../midi/MidiFile.h"
#include "../midi/MidiInput.h"
#include "../midi/MidiPlayer.h"
#include "../midi/MidiTrack.h"
#include "../midi/PlayerThread.h"
#include "../protocol/Protocol.h"
#include "../tool/EditorTool.h"
#include "../tool/EventTool.h"
#include "../tool/Selection.h"
#include "../tool/Tool.h"

#include <QList>
#include <QtCore/qmath.h>

#define NUM_LINES 139
#define PIXEL_PER_S 100
#define PIXEL_PER_LINE 11
#define PIXEL_PER_EVENT 15

MatrixWidget::MatrixWidget(QWidget* parent)
    : PaintWidget(parent)
{

    screen_locked = false;
    startTimeX = 0;
    startLineY = 50;
    endTimeX = 0;
    endLineY = 0;
    file = 0;
    scaleX = 1;
    pianoEvent = new NoteOnEvent(0, 100, 0, 0);
    scaleY = 1;
    lineNameWidth = 110;
    timeHeight = 50;
    _verticalMode = false;
    currentTempoEvents = new QList<MidiEvent*>;
    currentTimeSignatureEvents = new QList<TimeSignatureEvent*>;
    msOfFirstEventInList = 0;
    objects = new QList<MidiEvent*>;
    velocityObjects = new QList<MidiEvent*>;
    EditorTool::setMatrixWidget(this);
    setMouseTracking(true);
    setFocusPolicy(Qt::ClickFocus);

    setRepaintOnMouseMove(false);
    setRepaintOnMousePress(false);
    setRepaintOnMouseRelease(false);

    connect(MidiPlayer::playerThread(), SIGNAL(timeMsChanged(int)),
        this, SLOT(timeMsChanged(int)));

    pixmap = 0;
    _div = 2;
}

void MatrixWidget::setScreenLocked(bool b)
{
    screen_locked = b;
}

bool MatrixWidget::screenLocked()
{
    return screen_locked;
}

void MatrixWidget::timeMsChanged(int ms, bool ignoreLocked)
{

    if (!file)
        return;

    if (!_verticalMode) {
        int x = xPosOfMs(ms);
        if ((!screen_locked || ignoreLocked) && (x < lineNameWidth || ms < startTimeX || ms > endTimeX || x > width() - 100)) {
            if (file->maxTime() <= endTimeX && ms >= startTimeX) {
                repaint();
                return;
            }
            emit scrollChanged(ms, (file->maxTime() - endTimeX + startTimeX), startLineY,
                NUM_LINES - (endLineY - startLineY));
        } else {
            repaint();
        }
    } else {
        int y = xPosOfMs(ms); // returns Y in vertical mode
        int limit = height() - lineNameWidth - 100;
        if ((!screen_locked || ignoreLocked) && (y < 0 || ms < startTimeX || ms > endTimeX || y > limit)) {
            if (file->maxTime() <= endTimeX && ms >= startTimeX) {
                repaint();
                return;
            }
            emit scrollChanged(ms, (file->maxTime() - endTimeX + startTimeX), startLineY,
                NUM_LINES - (endLineY - startLineY));
        } else {
            repaint();
        }
    }
}

void MatrixWidget::scrollXChanged(int scrollPositionX)
{

    if (!file)
        return;

    startTimeX = scrollPositionX;
    if (!_verticalMode) {
        endTimeX = startTimeX + ((width() - lineNameWidth) * 1000) / (PIXEL_PER_S * scaleX);
    } else {
        endTimeX = startTimeX + ((height() - lineNameWidth) * 1000) / (PIXEL_PER_S * scaleX);
    }

    // more space than needed: scale x
    if (endTimeX - startTimeX > file->maxTime()) {
        endTimeX = file->maxTime();
        startTimeX = 0;
    } else if (startTimeX < 0) {
        endTimeX -= startTimeX;
        startTimeX = 0;
    } else if (endTimeX > file->maxTime()) {
        startTimeX += file->maxTime() - endTimeX;
        endTimeX = file->maxTime();
    }
    registerRelayout();
    repaint();
}

void MatrixWidget::scrollYChanged(int scrollPositionY)
{
    if (!file)
        return;

    startLineY = scrollPositionY;

    double space;
    if (!_verticalMode) {
        space = height() - timeHeight;
    } else {
        space = width() - timeHeight;
    }
    double lineSpace = scaleY * PIXEL_PER_LINE;
    double linesInWidget = space / lineSpace;
    endLineY = startLineY + linesInWidget;

    if (endLineY > NUM_LINES) {
        int d = endLineY - NUM_LINES;
        endLineY = NUM_LINES;
        startLineY -= d;
        if (startLineY < 0) {
            startLineY = 0;
        }
    }
    registerRelayout();
    repaint();
}

void MatrixWidget::paintEvent(QPaintEvent* event)
{

    if (!file)
        return;

    QPainter* painter = new QPainter(this);
    QFont font = painter->font();
    font.setPixelSize(12);
    painter->setFont(font);
    painter->setClipping(false);

    bool totalRepaint = !pixmap;

    if (totalRepaint) {
        this->pianoKeys.clear();
        pixmap = new QPixmap(width(), height());
        QPainter* pixpainter = new QPainter(pixmap);
        if (!QApplication::arguments().contains("--no-antialiasing")) {
            pixpainter->setRenderHint(QPainter::Antialiasing);
        }

        QFont f = pixpainter->font();
        f.setPixelSize(12);
        pixpainter->setFont(f);
        pixpainter->setClipping(false);

        for (int i = 0; i < objects->length(); i++) {
            objects->at(i)->setShown(false);
            OnEvent* onev = dynamic_cast<OnEvent*>(objects->at(i));
            if (onev && onev->offEvent()) {
                onev->offEvent()->setShown(false);
            }
        }
        objects->clear();
        velocityObjects->clear();
        currentTempoEvents->clear();
        currentTimeSignatureEvents->clear();
        currentDivs.clear();

        startTick = file->tick(startTimeX, endTimeX, &currentTempoEvents,
            &endTick, &msOfFirstEventInList);

        TempoChangeEvent* ev = dynamic_cast<TempoChangeEvent*>(
            currentTempoEvents->at(0));
        if (!ev) {
            pixpainter->fillRect(0, 0, width(), height(), Qt::red);
            delete pixpainter;
            return;
        }
        int numLines = endLineY - startLineY;
        if (numLines == 0) {
            delete pixpainter;
            return;
        }

        // dark gray background
        pixpainter->fillRect(0, 0, width(), height(), Qt::darkGray);

        if (!_verticalMode) {
            // ---- HORIZONTAL MODE ----

            // fill background of the line descriptions (piano area)
            pixpainter->fillRect(PianoArea, QApplication::palette().window());

            // fill the piano background white for note lines
            int pianoKeyCount = numLines;
            if (endLineY > 127) {
                pianoKeyCount -= (endLineY - 127);
            }
            if (pianoKeyCount > 0) {
                pixpainter->fillRect(0, timeHeight, lineNameWidth - 10,
                    pianoKeyCount * lineHeight(), Qt::white);
            }

            // draw row backgrounds
            for (int i = startLineY; i <= endLineY; i++) {
                int startLine = yPosOfLine(i);
                QColor c(194, 230, 255);
                if (!((1 << (i % 12)) & sharp_strip_mask)) {
                    c = QColor(234, 246, 255);
                }
                if (i > 127) {
                    c = QColor(194, 194, 194);
                    if (i % 2 == 1) c = QColor(234, 246, 255);
                }
                pixpainter->fillRect(lineNameWidth, startLine, width(),
                    startLine + lineHeight(), c);
            }

            // paint timeline area
            pixpainter->fillRect(0, 0, width(), timeHeight, QApplication::palette().window());

            pixpainter->setClipping(true);
            pixpainter->setClipRect(lineNameWidth, 0, width() - lineNameWidth - 2, height());

            pixpainter->setPen(Qt::darkGray);
            pixpainter->setBrush(Qt::white);
            pixpainter->drawRect(lineNameWidth, 2, width() - lineNameWidth - 1, timeHeight - 2);
            pixpainter->setPen(Qt::black);
            pixpainter->fillRect(0, timeHeight - 3, width(), 3, QApplication::palette().window());

            // paint time labels
            int numbers = (width() - lineNameWidth) / 80;
            if (numbers > 0) {
                int step = (endTimeX - startTimeX) / numbers;
                int realstep = 1, nextfak = 2, tenfak = 1;
                while (realstep <= step) {
                    realstep = nextfak * tenfak;
                    if (nextfak == 1) { nextfak++; continue; }
                    if (nextfak == 2) { nextfak = 5; continue; }
                    if (nextfak == 5) { nextfak = 1; tenfak *= 10; }
                }
                int startNumber = (startTimeX / realstep) * realstep;
                if (startNumber < startTimeX) startNumber += realstep;
                pixpainter->setPen(Qt::gray);
                while (startNumber < endTimeX) {
                    int pos = xPosOfMs(startNumber);
                    int hours = startNumber / (60000 * 60);
                    int remaining = startNumber - (60000 * 60) * hours;
                    int minutes = remaining / 60000;
                    remaining -= minutes * 60000;
                    int seconds = remaining / 1000;
                    int ms = remaining - 1000 * seconds;
                    QString text = QString::number(hours) + ":"
                        + QString("%1:").arg(minutes, 2, 10, QChar('0'))
                        + QString("%1").arg(seconds, 2, 10, QChar('0'))
                        + QString(".%1").arg(ms / 10, 2, 10, QChar('0'));
                    int textlength = QFontMetrics(pixpainter->font()).width(text);
                    if (startNumber > 0)
                        pixpainter->drawText(pos - textlength / 2, timeHeight / 2 - 6, text);
                    pixpainter->drawLine(pos, timeHeight / 2 - 1, pos, timeHeight);
                    startNumber += realstep;
                }
            }

            // draw measures
            int measure = file->measure(startTick, endTick, &currentTimeSignatureEvents);
            TimeSignatureEvent* currentEvent = currentTimeSignatureEvents->at(0);
            int i = 0;
            if (!currentEvent) { delete pixpainter; return; }
            int tick = currentEvent->midiTime();
            while (tick + currentEvent->ticksPerMeasure() <= startTick)
                tick += currentEvent->ticksPerMeasure();
            while (tick < endTick) {
                TimeSignatureEvent* measureEvent = currentTimeSignatureEvents->at(i);
                int xfrom = xPosOfMs(msOfTick(tick));
                currentDivs.append(QPair<int, int>(xfrom, tick));
                measure++;
                int measureStartTick = tick;
                tick += currentEvent->ticksPerMeasure();
                if (i < currentTimeSignatureEvents->length() - 1) {
                    if (currentTimeSignatureEvents->at(i + 1)->midiTime() <= tick) {
                        currentEvent = currentTimeSignatureEvents->at(i + 1);
                        tick = currentEvent->midiTime();
                        i++;
                    }
                }
                int xto = xPosOfMs(msOfTick(tick));
                pixpainter->setBrush(Qt::lightGray);
                pixpainter->setPen(Qt::NoPen);
                pixpainter->drawRoundedRect(xfrom + 2, timeHeight / 2 + 4, xto - xfrom - 4, timeHeight / 2 - 10, 5, 5);
                if (tick > startTick) {
                    pixpainter->setPen(Qt::gray);
                    pixpainter->drawLine(xfrom, timeHeight / 2, xfrom, height());
                    QString text = "Measure " + QString::number(measure - 1);
                    int textlength = QFontMetrics(pixpainter->font()).width(text);
                    if (textlength > xto - xfrom) {
                        text = QString::number(measure - 1);
                        textlength = QFontMetrics(pixpainter->font()).width(text);
                    }
                    int pos = (xfrom + xto) / 2;
                    pixpainter->setPen(Qt::white);
                    pixpainter->drawText(pos - textlength / 2, timeHeight - 9, text);
                    if (_div >= 0) {
                        double metronomeDiv = 4 / (double)qPow(2, _div);
                        int ticksPerDiv = metronomeDiv * file->ticksPerQuarter();
                        int startTickDiv = ticksPerDiv;
                        QPen oldPen = pixpainter->pen();
                        QPen dashPen = QPen(Qt::lightGray, 1, Qt::DashLine);
                        pixpainter->setPen(dashPen);
                        while (startTickDiv < measureEvent->ticksPerMeasure()) {
                            int divTick = startTickDiv + measureStartTick;
                            int xDiv = xPosOfMs(msOfTick(divTick));
                            currentDivs.append(QPair<int, int>(xDiv, divTick));
                            pixpainter->drawLine(xDiv, timeHeight, xDiv, height());
                            startTickDiv += ticksPerDiv;
                        }
                        pixpainter->setPen(oldPen);
                    }
                }
            }

            pixpainter->setPen(Qt::gray);
            pixpainter->drawLine(0, timeHeight, width(), timeHeight);
            pixpainter->drawLine(lineNameWidth, timeHeight, lineNameWidth, height());
            pixpainter->setPen(Qt::black);

            // paint notes
            pixpainter->setClipping(true);
            pixpainter->setClipRect(lineNameWidth, timeHeight, width() - lineNameWidth, height() - timeHeight);
            for (int i = 0; i < 19; i++) paintChannel(pixpainter, i);
            pixpainter->setClipping(false);

        } else {
            // ---- VERTICAL MODE ----
            // Layout: timeline strip on left (width=timeHeight), piano at bottom (height=lineNameWidth)
            // Note grid area: x=[timeHeight..width()], y=[0..height()-lineNameWidth]

            // Fill timeline strip (left side)
            pixpainter->fillRect(0, 0, timeHeight, height(), QApplication::palette().window());

            // Fill piano area (bottom)
            pixpainter->fillRect(timeHeight, height() - lineNameWidth, width() - timeHeight, lineNameWidth, Qt::white);

            // Draw pitch column backgrounds
            for (int i = startLineY; i <= endLineY; i++) {
                int startX = yPosOfLine(i); // X position for this pitch column
                QColor c(194, 230, 255);
                if (!((1 << (i % 12)) & sharp_strip_mask)) {
                    c = QColor(234, 246, 255);
                }
                if (i > 127) {
                    c = QColor(194, 194, 194);
                    if (i % 2 == 1) c = QColor(234, 246, 255);
                }
                pixpainter->fillRect(startX, 0, (int)lineWidth() + 1, height() - lineNameWidth, c);
            }

            // Draw timeline area (left strip) with box
            pixpainter->setClipping(true);
            pixpainter->setClipRect(0, 0, timeHeight, height() - lineNameWidth);

            pixpainter->setPen(Qt::darkGray);
            pixpainter->setBrush(Qt::white);
            pixpainter->drawRect(2, 0, timeHeight - 2, height() - lineNameWidth - 1);
            pixpainter->setPen(Qt::black);

            // Paint time labels (rotated, on left strip)
            int numbers = (height() - lineNameWidth) / 80;
            if (numbers > 0) {
                int step = (endTimeX - startTimeX) / numbers;
                int realstep = 1, nextfak = 2, tenfak = 1;
                while (realstep <= step) {
                    realstep = nextfak * tenfak;
                    if (nextfak == 1) { nextfak++; continue; }
                    if (nextfak == 2) { nextfak = 5; continue; }
                    if (nextfak == 5) { nextfak = 1; tenfak *= 10; }
                }
                int startNumber = (startTimeX / realstep) * realstep;
                if (startNumber < startTimeX) startNumber += realstep;
                pixpainter->setPen(Qt::gray);
                while (startNumber < endTimeX) {
                    int pos = xPosOfMs(startNumber); // Y position
                    int hours = startNumber / (60000 * 60);
                    int remaining = startNumber - (60000 * 60) * hours;
                    int minutes = remaining / 60000;
                    remaining -= minutes * 60000;
                    int seconds = remaining / 1000;
                    int ms = remaining - 1000 * seconds;
                    QString text = QString::number(hours) + ":"
                        + QString("%1:").arg(minutes, 2, 10, QChar('0'))
                        + QString("%1").arg(seconds, 2, 10, QChar('0'))
                        + QString(".%1").arg(ms / 10, 2, 10, QChar('0'));
                    // Draw time label rotated 90 degrees (bottom-to-top)
                    if (startNumber > 0) {
                        pixpainter->save();
                        pixpainter->translate(timeHeight / 2 - 4, pos);
                        pixpainter->rotate(-90);
                        int textlength = QFontMetrics(pixpainter->font()).width(text);
                        pixpainter->drawText(-textlength / 2, 0, text);
                        pixpainter->restore();
                    }
                    pixpainter->drawLine(timeHeight / 2, pos, timeHeight, pos);
                    startNumber += realstep;
                }
            }

            // Draw measures on left strip
            int measure = file->measure(startTick, endTick, &currentTimeSignatureEvents);
            TimeSignatureEvent* currentEvent = currentTimeSignatureEvents->at(0);
            int i = 0;
            if (!currentEvent) { delete pixpainter; return; }
            int tick = currentEvent->midiTime();
            while (tick + currentEvent->ticksPerMeasure() <= startTick)
                tick += currentEvent->ticksPerMeasure();

            // Also set clip for note grid when drawing measure lines across it
            pixpainter->setClipping(false);
            pixpainter->setClipping(true);
            pixpainter->setClipRect(0, 0, width(), height() - lineNameWidth);

            while (tick < endTick) {
                TimeSignatureEvent* measureEvent = currentTimeSignatureEvents->at(i);
                int yfrom = xPosOfMs(msOfTick(tick)); // Y position
                currentDivs.append(QPair<int, int>(yfrom, tick));
                measure++;
                int measureStartTick = tick;
                tick += currentEvent->ticksPerMeasure();
                if (i < currentTimeSignatureEvents->length() - 1) {
                    if (currentTimeSignatureEvents->at(i + 1)->midiTime() <= tick) {
                        currentEvent = currentTimeSignatureEvents->at(i + 1);
                        tick = currentEvent->midiTime();
                        i++;
                    }
                }
                int yto = xPosOfMs(msOfTick(tick)); // Y position

                // Draw measure label box in timeline strip
                pixpainter->setBrush(Qt::lightGray);
                pixpainter->setPen(Qt::NoPen);
                pixpainter->drawRoundedRect(timeHeight / 2 + 4, yfrom + 2, timeHeight / 2 - 10, yto - yfrom - 4, 5, 5);

                if (tick > startTick) {
                    pixpainter->setPen(Qt::gray);
                    // Horizontal line across full width at measure boundary
                    pixpainter->drawLine(timeHeight / 2, yfrom, width(), yfrom);

                    // Draw measure number, rotated in the left strip
                    QString text = "M" + QString::number(measure - 1);
                    int textlength = QFontMetrics(pixpainter->font()).width(text);
                    int midY = (yfrom + yto) / 2;
                    if (yto - yfrom >= 14) {
                        pixpainter->save();
                        pixpainter->translate(timeHeight - 9, midY);
                        pixpainter->rotate(-90);
                        pixpainter->setPen(Qt::white);
                        pixpainter->drawText(-textlength / 2, 0, text);
                        pixpainter->restore();
                    }

                    if (_div >= 0) {
                        double metronomeDiv = 4 / (double)qPow(2, _div);
                        int ticksPerDiv = metronomeDiv * file->ticksPerQuarter();
                        int startTickDiv = ticksPerDiv;
                        QPen oldPen = pixpainter->pen();
                        QPen dashPen = QPen(Qt::lightGray, 1, Qt::DashLine);
                        pixpainter->setPen(dashPen);
                        while (startTickDiv < measureEvent->ticksPerMeasure()) {
                            int divTick = startTickDiv + measureStartTick;
                            int yDiv = xPosOfMs(msOfTick(divTick));
                            currentDivs.append(QPair<int, int>(yDiv, divTick));
                            pixpainter->drawLine(timeHeight, yDiv, width(), yDiv);
                            startTickDiv += ticksPerDiv;
                        }
                        pixpainter->setPen(oldPen);
                    }
                }
            }

            pixpainter->setClipping(false);

            // Separator lines
            pixpainter->setPen(Qt::gray);
            pixpainter->drawLine(timeHeight, 0, timeHeight, height());
            pixpainter->drawLine(0, height() - lineNameWidth, width(), height() - lineNameWidth);
            pixpainter->setPen(Qt::black);

            // Paint notes in note grid area
            pixpainter->setClipping(true);
            pixpainter->setClipRect(timeHeight, 0, width() - timeHeight, height() - lineNameWidth);
            for (int i = 0; i < 19; i++) paintChannel(pixpainter, i);
            pixpainter->setClipping(false);
        }

        pixpainter->setPen(Qt::black);
        delete pixpainter;
    }

    painter->drawPixmap(0, 0, *pixmap);

    painter->setRenderHint(QPainter::Antialiasing);

    if (!_verticalMode) {
        // draw the piano / linenames (horizontal mode: left side)
        for (int i = startLineY; i <= endLineY; i++) {
            int startLine = yPosOfLine(i);
            if (i >= 0 && i <= 127) {
                paintPianoKey(painter, 127 - i, 0, startLine,
                    lineNameWidth, lineHeight());
            } else {
                QString text = "";
                switch (i) {
                case MidiEvent::CONTROLLER_LINE:      text = "Control Change"; break;
                case MidiEvent::TEMPO_CHANGE_EVENT_LINE: text = "Tempo Change"; break;
                case MidiEvent::TIME_SIGNATURE_EVENT_LINE: text = "Time Signature"; break;
                case MidiEvent::KEY_SIGNATURE_EVENT_LINE: text = "Key Signature."; break;
                case MidiEvent::PROG_CHANGE_LINE:     text = "Program Change"; break;
                case MidiEvent::KEY_PRESSURE_LINE:    text = "Key Pressure"; break;
                case MidiEvent::CHANNEL_PRESSURE_LINE: text = "Channel Pressure"; break;
                case MidiEvent::TEXT_EVENT_LINE:      text = "Text"; break;
                case MidiEvent::PITCH_BEND_LINE:      text = "Pitch Bend"; break;
                case MidiEvent::SYSEX_LINE:           text = "System Exclusive"; break;
                case MidiEvent::UNKNOWN_LINE:         text = "(Unknown)"; break;
                }
                painter->setPen(Qt::darkGray);
                font = painter->font();
                font.setPixelSize(10);
                painter->setFont(font);
                int textlength = QFontMetrics(font).width(text);
                painter->drawText(lineNameWidth - 15 - textlength, startLine + lineHeight(), text);
            }
        }
    } else {
        // draw piano keys at the bottom (vertical mode)
        for (int i = startLineY; i <= endLineY; i++) {
            int startX = yPosOfLine(i); // X position for this pitch column
            if (i >= 0 && i <= 127) {
                paintPianoKeyH(painter, 127 - i, startX, height() - lineNameWidth,
                    (int)lineWidth(), lineNameWidth);
            } else {
                // Draw label text for non-note lines at bottom
                QString text = "";
                switch (i) {
                case MidiEvent::CONTROLLER_LINE:      text = "CC"; break;
                case MidiEvent::TEMPO_CHANGE_EVENT_LINE: text = "Tmp"; break;
                case MidiEvent::TIME_SIGNATURE_EVENT_LINE: text = "TS"; break;
                case MidiEvent::KEY_SIGNATURE_EVENT_LINE: text = "KS"; break;
                case MidiEvent::PROG_CHANGE_LINE:     text = "PC"; break;
                case MidiEvent::KEY_PRESSURE_LINE:    text = "KP"; break;
                case MidiEvent::CHANNEL_PRESSURE_LINE: text = "ChP"; break;
                case MidiEvent::TEXT_EVENT_LINE:      text = "Txt"; break;
                case MidiEvent::PITCH_BEND_LINE:      text = "PB"; break;
                case MidiEvent::SYSEX_LINE:           text = "SX"; break;
                case MidiEvent::UNKNOWN_LINE:         text = "?"; break;
                }
                painter->setPen(Qt::darkGray);
                font = painter->font();
                font.setPixelSize(9);
                painter->setFont(font);
                painter->drawText(startX + 2, height() - 4, text);
            }
        }
    }

    if (Tool::currentTool()) {
        painter->setClipping(true);
        painter->setClipRect(ToolArea);
        Tool::currentTool()->draw(painter);
        painter->setClipping(false);
    }

    if (!_verticalMode) {
        if (enabled && mouseInRect(TimeLineArea)) {
            painter->setPen(Qt::red);
            painter->drawLine(mouseX, 0, mouseX, height());
            painter->setPen(Qt::black);
        }

        if (MidiPlayer::isPlaying()) {
            painter->setPen(Qt::red);
            int x = xPosOfMs(MidiPlayer::timeMs());
            if (x >= lineNameWidth) {
                painter->drawLine(x, 0, x, height());
            }
            painter->setPen(Qt::black);
        }

        // paint the cursorTick of file
        if (midiFile()->cursorTick() >= startTick && midiFile()->cursorTick() <= endTick) {
            painter->setPen(Qt::darkGray);
            int x = xPosOfMs(msOfTick(midiFile()->cursorTick()));
            painter->drawLine(x, 0, x, height());
            QPointF points[3] = {
                QPointF(x - 8, timeHeight / 2 + 2),
                QPointF(x + 8, timeHeight / 2 + 2),
                QPointF(x, timeHeight - 2),
            };
            painter->setBrush(QBrush(QColor(194, 230, 255), Qt::SolidPattern));
            painter->drawPolygon(points, 3);
            painter->setPen(Qt::gray);
        }

        // paint the pauseTick
        if (!MidiPlayer::isPlaying() && midiFile()->pauseTick() >= startTick && midiFile()->pauseTick() <= endTick) {
            int x = xPosOfMs(msOfTick(midiFile()->pauseTick()));
            QPointF points[3] = {
                QPointF(x - 8, timeHeight / 2 + 2),
                QPointF(x + 8, timeHeight / 2 + 2),
                QPointF(x, timeHeight - 2),
            };
            painter->setBrush(QBrush(Qt::gray, Qt::SolidPattern));
            painter->drawPolygon(points, 3);
        }

    } else {
        // vertical mode: playhead is a horizontal line

        if (enabled && mouseInRect(TimeLineArea)) {
            painter->setPen(Qt::red);
            painter->drawLine(0, mouseY, width(), mouseY);
            painter->setPen(Qt::black);
        }

        if (MidiPlayer::isPlaying()) {
            painter->setPen(Qt::red);
            int y = xPosOfMs(MidiPlayer::timeMs()); // Y position for current time
            if (y >= 0 && y <= height() - lineNameWidth) {
                painter->drawLine(timeHeight, y, width(), y);
            }
            painter->setPen(Qt::black);
        }

        // paint the cursorTick
        if (midiFile()->cursorTick() >= startTick && midiFile()->cursorTick() <= endTick) {
            painter->setPen(Qt::darkGray);
            int y = xPosOfMs(msOfTick(midiFile()->cursorTick()));
            painter->drawLine(0, y, width(), y);
            // Left-pointing triangle in the timeline strip
            QPointF points[3] = {
                QPointF(timeHeight / 2 - 2, y - 8),
                QPointF(timeHeight / 2 - 2, y + 8),
                QPointF(timeHeight - 2,     y),
            };
            painter->setBrush(QBrush(QColor(194, 230, 255), Qt::SolidPattern));
            painter->drawPolygon(points, 3);
            painter->setPen(Qt::gray);
        }

        // paint the pauseTick
        if (!MidiPlayer::isPlaying() && midiFile()->pauseTick() >= startTick && midiFile()->pauseTick() <= endTick) {
            int y = xPosOfMs(msOfTick(midiFile()->pauseTick()));
            QPointF points[3] = {
                QPointF(timeHeight / 2 - 2, y - 8),
                QPointF(timeHeight / 2 - 2, y + 8),
                QPointF(timeHeight - 2,     y),
            };
            painter->setBrush(QBrush(Qt::gray, Qt::SolidPattern));
            painter->drawPolygon(points, 3);
        }
    }

    // border
    painter->setPen(Qt::gray);
    painter->drawLine(width() - 1, height() - 1, lineNameWidth, height() - 1);
    painter->drawLine(width() - 1, height() - 1, width() - 1, 2);

    // if the recorder is recording, show red circle
    if (MidiInput::recording()) {
        painter->setBrush(Qt::red);
        painter->drawEllipse(width() - 20, timeHeight + 5, 15, 15);
    }
    delete painter;

    // if MouseRelease was not used, delete it
    mouseReleased = false;

    if (totalRepaint) {
        emit objectListChanged();
    }
}

void MatrixWidget::paintChannel(QPainter* painter, int channel)
{
    if (!file->channel(channel)->visible()) {
        return;
    }
    QColor cC = *file->channel(channel)->color();

    // filter events
    QMultiMap<int, MidiEvent*>* map = file->channelEvents(channel);

    QMap<int, MidiEvent*>::iterator it = map->lowerBound(startTick);
    while (it != map->end() && it.key() <= endTick) {
        MidiEvent* event = it.value();
        if (eventInWidget(event)) {
            // insert all Events in objects, set their coordinates
            // Only onEvents are inserted. When there is an On
            // and an OffEvent, the OnEvent will hold the coordinates
            int line = event->line();

            OffEvent* offEvent = dynamic_cast<OffEvent*>(event);
            OnEvent* onEvent = dynamic_cast<OnEvent*>(event);

            int evX, evY, evW, evH;

            if (!_verticalMode) {
                // Horizontal: x=time, y=pitch, w=duration, h=lineHeight
                evY = yPosOfLine(line);
                evH = lineHeight();
                if (onEvent || offEvent) {
                    if (onEvent) offEvent = onEvent->offEvent();
                    else if (offEvent) onEvent = dynamic_cast<OnEvent*>(offEvent->onEvent());
                    evW = xPosOfMs(msOfTick(offEvent->midiTime())) - xPosOfMs(msOfTick(onEvent->midiTime()));
                    evX = xPosOfMs(msOfTick(onEvent->midiTime()));
                    event = onEvent;
                    if (objects->contains(event)) { it++; continue; }
                } else {
                    evW = PIXEL_PER_EVENT;
                    evX = xPosOfMs(msOfTick(event->midiTime()));
                }
            } else {
                // Vertical: x=pitch (X), y=time (Y), w=lineWidth, h=duration
                evX = yPosOfLine(line); // pitch -> X
                evH = lineHeight();     // lineWidth in vertical mode
                if (onEvent || offEvent) {
                    if (onEvent) offEvent = onEvent->offEvent();
                    else if (offEvent) onEvent = dynamic_cast<OnEvent*>(offEvent->onEvent());
                    // time duration -> Y height
                    evW = lineHeight(); // pitch band width
                    evY = xPosOfMs(msOfTick(onEvent->midiTime())); // time start -> Y
                    evH = xPosOfMs(msOfTick(offEvent->midiTime())) - evY; // duration in Y pixels
                    evX = yPosOfLine(line); // pitch -> X
                    event = onEvent;
                    if (objects->contains(event)) { it++; continue; }
                } else {
                    evY = xPosOfMs(msOfTick(event->midiTime())); // time -> Y
                    evW = lineHeight(); // pitch band width
                    evH = PIXEL_PER_EVENT;
                    evX = yPosOfLine(line); // pitch -> X
                }
            }

            event->setX(evX);
            event->setY(evY);
            event->setWidth(evW);
            event->setHeight(evH);

            if (!(event->track()->hidden())) {
                if (!_colorsByChannels) {
                    cC = *event->track()->color();
                }
                event->draw(painter, cC);

                if (Selection::instance()->selectedEvents().contains(event)) {
                    painter->setPen(Qt::gray);
                    if (!_verticalMode) {
                        painter->drawLine(lineNameWidth, evY, this->width(), evY);
                        painter->drawLine(lineNameWidth, evY + evH, this->width(), evY + evH);
                    } else {
                        painter->drawLine(evX, 0, evX, height() - lineNameWidth);
                        painter->drawLine(evX + evW, 0, evX + evW, height() - lineNameWidth);
                    }
                    painter->setPen(Qt::black);
                }
                objects->prepend(event);
            }
        }

        if (!(event->track()->hidden())) {
            // append event to velocityObjects if not offEvent and in x-area
            OffEvent* offEvent = dynamic_cast<OffEvent*>(event);
            if (!offEvent && event->midiTime() >= startTick && event->midiTime() <= endTick && !velocityObjects->contains(event)) {
                event->setX(xPosOfMs(msOfTick(event->midiTime())));
                velocityObjects->prepend(event);
            }
        }
        it++;
    }
}

void MatrixWidget::paintPianoKey(QPainter* painter, int number, int x, int y,
    int width, int height)
{
    int borderRight = 10;
    width = width - borderRight;
    if (number >= 0 && number <= 127) {

        double scaleHeightBlack = 0.5;
        double scaleWidthBlack = 0.6;

        bool isBlack = false;
        bool blackOnTop = false;
        bool blackBeneath = false;
        QString name = "";

        switch (number % 12) {
        case 0: {
            // C
            blackOnTop = true;
            name = "";
            int i = number / 12;
            //if(i<4){
            //	name="C";{
            //		for(int j = 0; j<3-i; j++){
            //			name+="'";
            //		}
            //	}
            //} else {
            //	name = "c";
            //	for(int j = 0; j<i-4; j++){
            //		name+="'";
            //	}
            //}
            name = "C" + QString::number(i - 1);
            break;
        }
        // Cis
        case 1: {
            isBlack = true;
            break;
        }
        // D
        case 2: {
            blackOnTop = true;
            blackBeneath = true;
            break;
        }
        // Dis
        case 3: {
            isBlack = true;
            break;
        }
        // E
        case 4: {
            blackBeneath = true;
            break;
        }
        // F
        case 5: {
            blackOnTop = true;
            break;
        }
        // fis
        case 6: {
            isBlack = true;
            break;
        }
        // G
        case 7: {
            blackOnTop = true;
            blackBeneath = true;
            break;
        }
        // gis
        case 8: {
            isBlack = true;
            break;
        }
        // A
        case 9: {
            blackOnTop = true;
            blackBeneath = true;
            break;
        }
        // ais
        case 10: {
            isBlack = true;
            break;
        }
        // H
        case 11: {
            blackBeneath = true;
            break;
        }
        }

        if (127 - number == startLineY) {
            blackOnTop = false;
        }

        bool selected = mouseY >= y && mouseY <= y + height && mouseX > lineNameWidth && mouseOver;
        foreach (MidiEvent* event, Selection::instance()->selectedEvents()) {
            if (event->line() == 127 - number) {
                selected = true;
                break;
            }
        }

        QPolygon keyPolygon;

        bool inRect = false;
        if (isBlack) {
            painter->drawLine(x, y + height / 2, x + width, y + height / 2);
            y += (height - height * scaleHeightBlack) / 2;
            QRect playerRect;
            playerRect.setX(x);
            playerRect.setY(y);
            playerRect.setWidth(width * scaleWidthBlack);
            playerRect.setHeight(height * scaleHeightBlack + 0.5);
            QColor c = Qt::black;
            if (mouseInRect(playerRect)) {
                c = QColor(200, 200, 200);
                inRect = true;
            }
            painter->fillRect(playerRect, c);

            keyPolygon.append(QPoint(x, y));
            keyPolygon.append(QPoint(x, y + height * scaleHeightBlack));
            keyPolygon.append(QPoint(x + width * scaleWidthBlack, y + height * scaleHeightBlack));
            keyPolygon.append(QPoint(x + width * scaleWidthBlack, y));
            pianoKeys.insert(number, playerRect);

        } else {

            if (!blackOnTop) {
                keyPolygon.append(QPoint(x, y));
                keyPolygon.append(QPoint(x + width, y));
            } else {
                keyPolygon.append(QPoint(x, y - height * scaleHeightBlack / 2));
                keyPolygon.append(QPoint(x + width * scaleWidthBlack, y - height * scaleHeightBlack / 2));
                keyPolygon.append(QPoint(x + width * scaleWidthBlack, y - height * scaleHeightBlack));
                keyPolygon.append(QPoint(x + width, y - height * scaleHeightBlack));
            }
            if (!blackBeneath) {
                painter->drawLine(x, y + height, x + width, y + height);
                keyPolygon.append(QPoint(x + width, y + height));
                keyPolygon.append(QPoint(x, y + height));
            } else {
                keyPolygon.append(QPoint(x + width, y + height + height * scaleHeightBlack));
                keyPolygon.append(QPoint(x + width * scaleWidthBlack, y + height + height * scaleHeightBlack));
                keyPolygon.append(QPoint(x + width * scaleWidthBlack, y + height + height * scaleHeightBlack / 2));
                keyPolygon.append(QPoint(x, y + height + height * scaleHeightBlack / 2));
            }
            inRect = mouseInRect(x, y, width, height);
            pianoKeys.insert(number, QRect(x, y, width, height));
        }

        if (isBlack) {
            if (inRect) {
                painter->setBrush(Qt::lightGray);
            } else if (selected) {
                painter->setBrush(Qt::darkGray);
            } else {
                painter->setBrush(Qt::black);
            }
        } else {
            if (inRect) {
                painter->setBrush(Qt::darkGray);
            } else if (selected) {
                painter->setBrush(Qt::lightGray);
            } else {
                painter->setBrush(Qt::white);
            }
        }
        painter->setPen(Qt::darkGray);
        painter->drawPolygon(keyPolygon, Qt::OddEvenFill);

        if (name != "") {
            painter->setPen(Qt::gray);
            int textlength = QFontMetrics(painter->font()).width(name);
            painter->drawText(x + width - textlength - 2, y + height - 1, name);
            painter->setPen(Qt::black);
        }
        if (inRect && enabled) {
            // mark the current Line
            QColor lineColor = QColor(0, 0, 100, 40);
            painter->fillRect(x + width + borderRight, yPosOfLine(127 - number),
                this->width() - x - width - borderRight, height, lineColor);
        }
    }
}

void MatrixWidget::setFile(MidiFile* f)
{

    file = f;

    scaleX = 1;
    scaleY = 1;

    startTimeX = 0;
    // Roughly vertically center on Middle C.
    startLineY = 50;

    connect(file->protocol(), SIGNAL(actionFinished()), this,
        SLOT(registerRelayout()));
    connect(file->protocol(), SIGNAL(actionFinished()), this, SLOT(update()));

    calcSizes();

    // scroll down to see events
    int maxNote = -1;
    for (int channel = 0; channel < 16; channel++) {

        QMultiMap<int, MidiEvent*>* map = file->channelEvents(channel);

        QMap<int, MidiEvent*>::iterator it = map->lowerBound(0);
        while (it != map->end()) {
            NoteOnEvent* onev = dynamic_cast<NoteOnEvent*>(it.value());
            if (onev && eventInWidget(onev)) {
                if (onev->line() < maxNote || maxNote < 0) {
                    maxNote = onev->line();
                }
            }
            it++;
        }
    }

    if (maxNote - 5 > 0) {
        startLineY = maxNote - 5;
    }

    calcSizes();
}

void MatrixWidget::calcSizes()
{
    if (!file) {
        return;
    }
    int time = file->maxTime();
    int timeInWidget = ((width() - lineNameWidth) * 1000) / (PIXEL_PER_S * scaleX);

    ToolArea = QRectF(lineNameWidth, timeHeight, width() - lineNameWidth,
        height() - timeHeight);
    PianoArea = QRectF(0, timeHeight, lineNameWidth, height() - timeHeight);
    TimeLineArea = QRectF(lineNameWidth, 0, width() - lineNameWidth, timeHeight);

    scrollXChanged(startTimeX);
    scrollYChanged(startLineY);

    emit sizeChanged(time - timeInWidget, NUM_LINES - endLineY + startLineY, startTimeX,
        startLineY);
}

MidiFile* MatrixWidget::midiFile()
{
    return file;
}

void MatrixWidget::mouseMoveEvent(QMouseEvent* event)
{
    PaintWidget::mouseMoveEvent(event);

    if (!enabled) {
        return;
    }

    if (!MidiPlayer::isPlaying() && Tool::currentTool()) {
        Tool::currentTool()->move(event->x(), event->y());
    }

    if (!MidiPlayer::isPlaying()) {
        repaint();
    }
}

void MatrixWidget::resizeEvent(QResizeEvent* event)
{
    Q_UNUSED(event);
    calcSizes();
}

int MatrixWidget::xPosOfMs(int ms)
{
    return lineNameWidth + (ms - startTimeX) * (width() - lineNameWidth) / (endTimeX - startTimeX);
}

int MatrixWidget::yPosOfLine(int line)
{
    return timeHeight + (line - startLineY) * lineHeight();
}

double MatrixWidget::lineHeight()
{
    if (endLineY - startLineY == 0)
        return 0;
    return (double)(height() - timeHeight) / (double)(endLineY - startLineY);
}

void MatrixWidget::enterEvent(QEvent* event)
{
    PaintWidget::enterEvent(event);
    if (Tool::currentTool()) {
        Tool::currentTool()->enter();
        if (enabled) {
            update();
        }
    }
}
void MatrixWidget::leaveEvent(QEvent* event)
{
    PaintWidget::leaveEvent(event);
    if (Tool::currentTool()) {
        Tool::currentTool()->exit();
        if (enabled) {
            update();
        }
    }
}
void MatrixWidget::mousePressEvent(QMouseEvent* event)
{
    PaintWidget::mousePressEvent(event);
    if (!MidiPlayer::isPlaying() && Tool::currentTool() && mouseInRect(ToolArea)) {
        if (Tool::currentTool()->press(event->buttons() == Qt::LeftButton)) {
            if (enabled) {
                update();
            }
        }
    } else if (enabled && (!MidiPlayer::isPlaying()) && (mouseInRect(PianoArea))) {
        foreach (int key, pianoKeys.keys()) {
            bool inRect = mouseInRect(pianoKeys.value(key));
            if (inRect) {
                // play note
                pianoEvent->setNote(key);
                MidiPlayer::play(pianoEvent);
            }
        }
    }
}
void MatrixWidget::mouseReleaseEvent(QMouseEvent* event)
{
    PaintWidget::mouseReleaseEvent(event);
    if (!MidiPlayer::isPlaying() && Tool::currentTool() && mouseInRect(ToolArea)) {
        if (Tool::currentTool()->release()) {
            if (enabled) {
                update();
            }
        }
    } else if (Tool::currentTool()) {
        if (Tool::currentTool()->releaseOnly()) {
            if (enabled) {
                update();
            }
        }
    }
}

void MatrixWidget::takeKeyPressEvent(QKeyEvent* event)
{

    if (Tool::currentTool()) {
        if (Tool::currentTool()->pressKey(event->key())) {
            repaint();
        }
    }
}

void MatrixWidget::takeKeyReleaseEvent(QKeyEvent* event)
{
    if (Tool::currentTool()) {
        if (Tool::currentTool()->releaseKey(event->key())) {
            repaint();
        }
    }
}

QList<MidiEvent*>* MatrixWidget::activeEvents()
{
    return objects;
}

QList<MidiEvent*>* MatrixWidget::velocityEvents()
{
    return velocityObjects;
}

int MatrixWidget::msOfXPos(int x)
{
    return startTimeX + ((x - lineNameWidth) * (endTimeX - startTimeX)) / (width() - lineNameWidth);
}

int MatrixWidget::msOfTick(int tick)
{
    return file->msOfTick(tick, currentTempoEvents, msOfFirstEventInList);
}

int MatrixWidget::timeMsOfWidth(int w)
{
    return (w * (endTimeX - startTimeX)) / (width() - lineNameWidth);
}

bool MatrixWidget::eventInWidget(MidiEvent* event)
{
    NoteOnEvent* on = dynamic_cast<NoteOnEvent*>(event);
    OffEvent* off = dynamic_cast<OffEvent*>(event);
    if (on) {
        off = on->offEvent();
    } else if (off) {
        on = dynamic_cast<NoteOnEvent*>(off->onEvent());
    }
    if (on && off) {
        int line = off->line();
        int tick = off->midiTime();
        bool offIn = line >= startLineY && line <= endLineY && tick >= startTick && tick <= endTick;
        line = on->line();
        tick = on->midiTime();
        bool onIn = line >= startLineY && line <= endLineY && tick >= startTick && tick <= endTick;

        off->setShown(offIn);
        on->setShown(onIn);

        return offIn || onIn;

    } else {
        int line = event->line();
        int tick = event->midiTime();
        bool shown = line >= startLineY && line <= endLineY && tick >= startTick && tick <= endTick;
        event->setShown(shown);

        return shown;
    }
}

int MatrixWidget::lineAtY(int y)
{
    return (y - timeHeight) / lineHeight() + startLineY;
}

void MatrixWidget::zoomStd()
{
    scaleX = 1;
    scaleY = 1;
    calcSizes();
}

void MatrixWidget::zoomHorIn()
{
    scaleX += 0.1;
    calcSizes();
}

void MatrixWidget::zoomHorOut()
{
    if (scaleX >= 0.2) {
        scaleX -= 0.1;
        calcSizes();
    }
}

void MatrixWidget::zoomVerIn()
{
    scaleY += 0.1;
    calcSizes();
}

void MatrixWidget::zoomVerOut()
{
    if (scaleY >= 0.2) {
        scaleY -= 0.1;
        if (height() <= NUM_LINES * lineHeight() * scaleY / (scaleY + 0.1)) {
            calcSizes();
        } else {
            scaleY += 0.1;
        }
    }
}

void MatrixWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (mouseInRect(TimeLineArea)) {
        int tick = file->tick(msOfXPos(mouseX));
        file->setCursorTick(tick);
        update();
    }
}

void MatrixWidget::registerRelayout()
{
    delete pixmap;
    pixmap = 0;
}

int MatrixWidget::minVisibleMidiTime()
{
    return startTick;
}

int MatrixWidget::maxVisibleMidiTime()
{
    return endTick;
}

void MatrixWidget::wheelEvent(QWheelEvent* event)
{
    /*
     * Qt has some underdocumented behaviors for reporting wheel events, so the
     * following were determined empirically:
     *
     * 1.  Some platforms use pixelDelta and some use angleDelta; you need to
     *     handle both.
     *
     * 2.  The documentation for angleDelta is very convoluted, but it boils
     *     down to a scaling factor of 8 to convert to pixels.  Note that
     *     some mouse wheels scroll very coarsely, but this should result in an
     *     equivalent amount of movement as seen in other programs, even when
     *     that means scrolling by multiple lines at a time.
     *
     * 3.  When a modifier key is held, the X and Y may be swapped in how
     *     they're reported, but which modifiers these are differ by platform.
     *     If you want to reserve the modifiers for your own use, you have to
     *     counteract this explicitly.
     *
     * 4.  A single-dimensional scrolling device (mouse wheel) seems to be
     *     reported in the Y dimension of the pixelDelta or angleDelta, but is
     *     subject to the same X/Y swapping when modifiers are pressed.
     */

    Qt::KeyboardModifiers km = event->modifiers();
    QPoint pixelDelta = event->pixelDelta();
    int pixelDeltaX = pixelDelta.x();
    int pixelDeltaY = pixelDelta.y();

    if ((pixelDeltaX == 0) && (pixelDeltaY == 0)) {
        QPoint angleDelta = event->angleDelta();
        pixelDeltaX = angleDelta.x() / 8;
        pixelDeltaY = angleDelta.y() / 8;
    }

    int horScrollAmount = 0;
    int verScrollAmount = 0;

    if (km) {
        int pixelDeltaLinear = pixelDeltaY;
        if (pixelDeltaLinear == 0) pixelDeltaLinear = pixelDeltaX;

        if (km == Qt::ShiftModifier) {
            if (pixelDeltaLinear > 0) {
                zoomVerIn();
            } else if (pixelDeltaLinear < 0) {
                zoomVerOut();
            }
        } else if (km == Qt::ControlModifier) {
            if (pixelDeltaLinear > 0) {
                zoomHorIn();
            } else if (pixelDeltaLinear < 0) {
                zoomHorOut();
            }
        } else if (km == Qt::AltModifier) {
            horScrollAmount = pixelDeltaLinear;
        }
    } else {
        horScrollAmount = pixelDeltaX;
        verScrollAmount = pixelDeltaY;
    }

    if (file) {
        int maxTimeInFile = file->maxTime();
        int widgetRange = endTimeX - startTimeX;

        if (horScrollAmount != 0) {
            int scroll = -1 * horScrollAmount * widgetRange / 1000;

            int newStartTime = startTimeX + scroll;

            scrollXChanged(newStartTime);
            emit scrollChanged(startTimeX, maxTimeInFile - widgetRange, startLineY, NUM_LINES - (endLineY - startLineY));
        }

        if (verScrollAmount != 0) {
            int newStartLineY = startLineY - (verScrollAmount / (scaleY * PIXEL_PER_LINE));

            if (newStartLineY < 0)
                newStartLineY = 0;

            // endline too large handled in scrollYchanged()
            scrollYChanged(newStartLineY);
            emit scrollChanged(startTimeX, maxTimeInFile - widgetRange, startLineY, NUM_LINES - (endLineY - startLineY));
        }
    }
}

void MatrixWidget::keyPressEvent(QKeyEvent* event)
{
    takeKeyPressEvent(event);
}

void MatrixWidget::keyReleaseEvent(QKeyEvent* event)
{
    takeKeyReleaseEvent(event);
}

void MatrixWidget::setColorsByChannel()
{
    _colorsByChannels = true;
}
void MatrixWidget::setColorsByTracks()
{
    _colorsByChannels = false;
}

bool MatrixWidget::colorsByChannel()
{
    return _colorsByChannels;
}

void MatrixWidget::setDiv(int div)
{
    _div = div;
    registerRelayout();
    update();
}

QList<QPair<int, int> > MatrixWidget::divs()
{
    return currentDivs;
}

int MatrixWidget::div()
{
    return _div;
}
