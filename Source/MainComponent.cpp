#include "MainComponent.h"
#include "juce_core/juce_core.h"
#include "juce_events/juce_events.h"
#include "juce_graphics/juce_graphics.h"
#include "juce_gui_basics/juce_gui_basics.h"
#include <cstdlib>

#define WAVEFORM_AREA 310
#define HEADER_HEIGHT 60
#define WAVEFORM_HEIGHT 300
#define MARGIN 20

struct ModernLookAndFeel : public juce::LookAndFeel_V4 {
    ModernLookAndFeel() {
        setColour(juce::ResizableWindow::backgroundColourId,
                  juce::Colour(0xff121212));
        setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a2a2a));
        setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        setColour(juce::TextEditor::backgroundColourId,
                  juce::Colour(0xff000000));
        setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff333333));
        setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff000000));
        setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff333333));
    }

    void drawButtonBackground(juce::Graphics &g, juce::Button &button,
                              const juce::Colour &backgroundColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override {
        auto cornerSize = 4.0f;
        auto bounds = button.getLocalBounds().toFloat();

        auto baseColour = backgroundColour;
        if (shouldDrawButtonAsDown)
            baseColour = baseColour.darker(0.2f);
        else if (shouldDrawButtonAsHighlighted)
            baseColour = baseColour.brighter(0.2f);

        g.setColour(baseColour);
        g.fillRoundedRectangle(bounds, cornerSize);

        g.setColour(juce::Colours::white.withAlpha(0.1f));
        g.drawRoundedRectangle(bounds, cornerSize, 1.0f);
    }

    void drawTextEditorOutline(juce::Graphics &g, int width, int height,
                               juce::TextEditor &textEditor) override {
        g.setColour(findColour(juce::TextEditor::outlineColourId));
        g.drawRoundedRectangle(0.5f, 0.5f, width - 1.0f, height - 1.0f, 4.0f,
                               1.0f);
    }

    void drawComboBox(juce::Graphics &g, int width, int height, bool, int, int,
                      int, int, juce::ComboBox &box) override {
        auto cornerSize = 4.0f;

        g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle(0, 0, width, height, cornerSize);
        g.setColour(box.findColour(juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle(0.5f, 0.5f, width - 1.0f, height - 1.0f,
                               cornerSize, 1.0f);
    }
};

MainComponent::MainComponent()
    : thumbnail(1000, formatManager, thumbnailCache) {

    setLookAndFeel(&modernLookAndFeel);

    eventBuffer.resize(256);
    recordedEvents.reserve(256);

    formatManager.registerBasicFormats(); // enables WAV, AIFF, MP3
    thumbnail.addChangeListener(this);    // notify when waveform is ready

    setSize(800, 600);

    setAudioChannels(0, 2);

    addAndMakeVisible(settingsButton);
    settingsButton.setButtonText("Audio Config");
    settingsButton.addListener(this);

    addAndMakeVisible(recordButton);
    recordButton.setButtonText("REC");
    recordButton.setColour(juce::TextButton::buttonColourId,
                           juce::Colour(0xffe91e63));
    recordButton.addListener(this);

    addAndMakeVisible(bpmLabel);
    addAndMakeVisible(bpmEditor);
    bpmEditor.setText("90");
    bpmEditor.setInputRestrictions(3, "0123456789");

    addAndMakeVisible(barsLabel);
    addAndMakeVisible(barsBox);

    barsBox.addItem("2 Bars", 1);
    barsBox.addItem("4 Bars", 2);
    barsBox.addItem("8 Bars", 3);

    barsBox.setSelectedId(2);
    barsBox.addListener(this);

    setWantsKeyboardFocus(true);

    startTimerHz(60);
}

MainComponent::~MainComponent() {
    setLookAndFeel(nullptr);
    shutdownAudio();
}

void MainComponent::prepareToPlay(int samplesPerBlockExpected,
                                  double sampleRate) {
    this->sampleRate = sampleRate;
    transportSource.prepareToPlay(samplesPerBlockExpected, sampleRate);
}

void MainComponent::releaseResources() { transportSource.releaseResources(); }

void MainComponent::resized() {
    auto area = getLocalBounds().reduced(MARGIN);

    auto headerArea = area.removeFromTop(40);

    int itemHeight = 30;
    int spacing = 10;

    recordButton.setBounds(
        headerArea.removeFromLeft(80).withHeight(itemHeight));
    headerArea.removeFromLeft(spacing * 2);

    bpmLabel.setBounds(headerArea.removeFromLeft(40).withHeight(itemHeight));
    bpmEditor.setBounds(headerArea.removeFromLeft(50).withHeight(itemHeight));

    headerArea.removeFromLeft(spacing);

    barsLabel.setBounds(headerArea.removeFromLeft(60).withHeight(itemHeight));
    barsBox.setBounds(headerArea.removeFromLeft(90).withHeight(itemHeight));

    settingsButton.setBounds(area.getRight() - 100, MARGIN, 100, itemHeight);
}

void MainComponent::changeListenerCallback(juce::ChangeBroadcaster *source) {
    if (source == &thumbnail) {
        repaint();
    }
}

void MainComponent::getNextAudioBlock(
    const juce::AudioSourceChannelInfo &bufferToFill) {
    const juce::ScopedLock sl(wrapperLock);

    bufferToFill.clearActiveBufferRegion();

    if (memorySource.get() != nullptr && !isCountingIn) {
        transportSource.getNextAudioBlock(bufferToFill);
    }

    addMetronomeToBuffer(bufferToFill);
}

bool MainComponent::isInterestedInFileDrag(const juce::StringArray &files) {
    return true; // accept all files
}

void MainComponent::filesDropped(const juce::StringArray &files, int x, int y) {
    juce::Logger::writeToLog("Debug: Files Dropped");

    if (files.size() == 1) {
        juce::Logger::writeToLog("Debug: Loading file: " + files[0]);

        loadFile(juce::File(files[0]));

        grabKeyboardFocus();
    }
}

void MainComponent::loadFile(const juce::File &file) {
    auto *reader = formatManager.createReaderFor(file);
    if (reader != nullptr) {
        transportSource.stop();

        // enter lock
        {
            const juce::ScopedLock sl(wrapperLock);

            transportSource.setSource(nullptr);
            memorySource.reset();

            double ratio = sampleRate / reader->sampleRate;
            int newLength =
                (int)(reader->lengthInSamples * ratio) + 1024; // +padding

            fileBuffer.setSize((int)reader->numChannels,
                               (int)reader->lengthInSamples);

            if (ratio == 1.0) {
                // exact match
                reader->read(&fileBuffer, 0, (int)reader->lengthInSamples, 0,
                             true, true);
            } else {
                // resample
                //
                // read original source into temporary buffer
                juce::AudioBuffer<float> tempBuffer(
                    (int)reader->numChannels, (int)reader->lengthInSamples);

                reader->read(&tempBuffer, 0, (int)reader->lengthInSamples, 0,
                             true, true);

                juce::MemoryAudioSource tempSource(tempBuffer, false, false);
                juce::ResamplingAudioSource resampler(&tempSource, false,
                                                      (int)reader->numChannels);

                resampler.setResamplingRatio(reader->sampleRate / sampleRate);
                resampler.prepareToPlay(newLength, sampleRate);

                juce::AudioSourceChannelInfo info(&fileBuffer, 0, newLength);
                resampler.getNextAudioBlock(info);
            }

            memorySource.reset(new juce::MemoryAudioSource(fileBuffer, false));

            transportSource.setSource(memorySource.get(), 0, nullptr,
                                      sampleRate);
        }

        thumbnail.setSource(new juce::FileInputSource(file));

        chopPositions.clear();

        double totalLength = transportSource.getLengthInSeconds();
        double chopLength = totalLength / 16.0;

        for (int i = 0; i < 16; ++i) {
            chopPositions.push_back(i * chopLength);
        }

        visibleStartTime = 0.0;
        visibleDuration = totalLength;

        repaint();
        delete reader;
    }
}

void MainComponent::paint(juce::Graphics &g) {
    g.fillAll(juce::Colour(0xff121212));

    auto area = getLocalBounds().reduced(MARGIN);
    auto headerArea = area.removeFromTop(40);

    area.removeFromTop(10); // spacing

    auto waveArea = area.removeFromTop(250);

    g.setColour(juce::Colour(0xff1e1e1e));
    g.fillRoundedRectangle(waveArea.toFloat(), 6.0f);

    g.setColour(juce::Colour(0xff333333));
    g.drawRoundedRectangle(waveArea.toFloat(), 6.0f, 1.0f);

    if (thumbnail.getNumChannels() == 0) {
        g.setColour(juce::Colours::grey);
        g.setFont(18.0f);

        g.drawText("Drop Audio File Here", waveArea,
                   juce::Justification::centred, true);
    } else {
        auto innerWave = waveArea.reduced(2);

        g.setColour(juce::Colour(0xff00d4ff));
        thumbnail.drawChannels(g, innerWave, visibleStartTime,
                               visibleStartTime + visibleDuration, 1.0f);

        for (int i = 0; i < chopPositions.size(); ++i) {
            double time = chopPositions[i];

            if (time < visibleStartTime ||
                time > (visibleStartTime + visibleDuration))
                continue;

            double timeFromStart = time - visibleStartTime;
            float xPos = innerWave.getX() + (timeFromStart / visibleDuration) *
                                                innerWave.getWidth();

            bool isSelected = (i == selectedChopIndex);

            g.setColour(isSelected ? juce::Colours::yellow
                                   : juce::Colours::white.withAlpha(0.3f));
            g.drawVerticalLine((int)xPos, innerWave.getY(),
                               innerWave.getBottom());

            if (isSelected) {
                juce::Path flag;
                flag.addTriangle(xPos - 5, innerWave.getY(), xPos + 5,
                                 innerWave.getY(), xPos, innerWave.getY() + 10);

                g.setColour(juce::Colours::yellow);
                g.fillPath(flag);
            }
        }
    }

    if (isCountingIn) {
        g.setColour(juce::Colours::black.withAlpha(0.8f));
        g.fillRoundedRectangle(waveArea.toFloat(), 6.0f);

        g.setColour(juce::Colour(0xffe91e63)); // Hot pink
        g.setFont(juce::Font(80.0f, juce::Font::bold));

        g.drawText(juce::String(5 - countInBeats), waveArea,
                   juce::Justification::centred, true);
    }

    area.removeFromTop(20); // spacing

    int cols = 8;
    int rows = 2;
    int gap = 8;
    int padWidth = (area.getWidth() - (gap * (cols - 1))) / cols;
    int padHeight = (area.getHeight() - gap) / rows;

    juce::String alphaKeys = "qwertyui";

    for (int i = 0; i < 16; ++i) {
        int r = i / 8;
        int c = i % 8;

        auto padBounds =
            juce::Rectangle<int>(area.getX() + c * (padWidth + gap),
                                 area.getY() + r * (padHeight + gap), padWidth,
                                 padHeight)
                .toFloat();

        bool isActive = (i == activePadIndex);
        bool isSelected = (i == selectedChopIndex);

        juce::Colour padColor;

        if (isActive)
            padColor = juce::Colour(0xff00d4ff); // active cyan
        else if (isSelected)
            padColor = juce::Colour(0xff444444); // selected gray
        else
            padColor = juce::Colour(0xff2a2a2a); // inactive dark

        g.setColour(padColor);
        g.fillRoundedRectangle(padBounds, 4.0f);

        // pad border
        if (isSelected) {
            g.setColour(juce::Colours::yellow);
            g.drawRoundedRectangle(padBounds, 4.0f, 2.0f);
        }

        juce::String id = (i < 8)
                              ? juce::String(i + 1)
                              : juce::String::charToString(alphaKeys[i - 8]);

        g.setColour(isActive ? juce::Colours::black : juce::Colours::white);
        g.setFont(juce::Font(20.0f, juce::Font::bold));

        g.drawText(id, padBounds, juce::Justification::centred, true);
    }
}

void MainComponent::mouseDown(const juce::MouseEvent &e) {
    auto area = getLocalBounds().reduced(MARGIN);
    area.removeFromTop(40 + 10);

    auto waveArea = area.removeFromTop(250);

    if (!waveArea.contains(e.position.toInt()))
        return;

    auto innerWave = waveArea.reduced(2);

    const float mouseX = e.position.x;
    const float hitPointRadiusPx = 8.0f;

    int closestIndex = -1;
    float minPixelDistance = hitPointRadiusPx;

    for (int i = 0; i < chopPositions.size(); ++i) {
        double time = chopPositions[i];

        if (time < visibleStartTime ||
            time > visibleStartTime + visibleDuration)
            continue;

        float xPos = innerWave.getX() +
                     float((time - visibleStartTime) / visibleDuration) *
                         innerWave.getWidth();

        float dist = std::abs(mouseX - xPos);

        if (dist < minPixelDistance) {
            minPixelDistance = dist;
            closestIndex = i;
        }
    }

    if (closestIndex != -1) {
        selectedChopIndex = closestIndex;
        isDraggingChop = true;
    } else {
        selectedChopIndex = -1;
    }

    repaint();
}

void MainComponent::mouseDrag(const juce::MouseEvent &e) {
    if (!isDraggingChop || selectedChopIndex == -1)
        return;

    auto waveArea = getLocalBounds().reduced(MARGIN);

    waveArea.removeFromTop(50);
    waveArea = waveArea.removeFromTop(250);

    float mouseX = juce::jlimit((float)waveArea.getX(),
                                (float)waveArea.getRight(), e.position.x);

    double timeAtMouse =
        visibleStartTime +
        ((mouseX - waveArea.getX()) / waveArea.getWidth()) * visibleDuration;

    chopPositions[selectedChopIndex] =
        juce::jlimit(0.0, thumbnail.getTotalLength(), timeAtMouse);

    repaint();
}

void MainComponent::mouseUp(const juce::MouseEvent &) {
    isDraggingChop = false;
}

void MainComponent::mouseWheelMove(const juce::MouseEvent &e,
                                   const juce::MouseWheelDetails &wheel) {
    if (thumbnail.getTotalLength() <= 0)
        return;

    // zoom with scroll wheel

    double zoomFactor = (wheel.deltaY > 0) ? 0.8 : 1.2;
    double newDuration = visibleDuration * zoomFactor;

    newDuration = juce::jlimit(0.1, thumbnail.getTotalLength(), newDuration);

    // center zoom on mouse pointer
    float mouseRatio = (e.x - 10) / (float)(getWidth() - 20);
    double mouseTime = visibleStartTime + (visibleDuration * mouseRatio);

    visibleStartTime = mouseTime - (newDuration * mouseRatio);
    visibleDuration = newDuration;

    // clamp start time
    if (visibleStartTime < 0)
        visibleStartTime = 0;

    if (visibleStartTime + visibleDuration > thumbnail.getTotalLength())
        visibleStartTime = thumbnail.getTotalLength() - visibleDuration;

    repaint();
}

bool MainComponent::keyPressed(const juce::KeyPress &key) {
    // stop playhead
    if (key == juce::KeyPress::spaceKey) {
        if (isRecording)
            stopRecording();

        transportSource.stop();

        return true;
    }

    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'r') {
        if (isRecording)
            stopRecording();
        else
            startRecording();

        return true;
    }

    if (selectedChopIndex != -1) {
        if (key == juce::KeyPress::leftKey) {
            chopPositions[selectedChopIndex] -= 0.01; // nudge back 10ms

            transportSource.setPosition(
                chopPositions[selectedChopIndex]); // preview
            transportSource.start();

            repaint();

            return true;
        }

        if (key == juce::KeyPress::rightKey) {
            chopPositions[selectedChopIndex] += 0.01; // nudge forward 10ms

            transportSource.setPosition(
                chopPositions[selectedChopIndex]); // preview
            transportSource.start();

            repaint();

            return true;
        }
    }

    int padIndex = -1;
    juce::juce_wchar k = key.getTextCharacter();

    if (k >= '1' && k <= '8') {
        padIndex = k - '1';
    } else {
        juce::String keys = "qwertyui";

        int index = keys.indexOfChar(k);
        if (index >= 0) {
            padIndex = 8 + index;
        }
    }

    if (padIndex >= 0 && padIndex < chopPositions.size()) {
        selectedChopIndex = padIndex; // select for editing

        double chopTime = chopPositions[padIndex];
        visibleStartTime = chopTime - 0.2;

        if (visibleStartTime < 0)
            visibleStartTime = 0;

        visibleDuration = 2.0; // show 2 seconds of audio

        transportSource.setPosition(chopPositions[padIndex]);
        transportSource.start();

        if (isRecording || isCountingIn) {
            auto write = eventFifo.write(1);

            if (write.blockSize1 > 0) {
                const int writeIndex = write.startIndex1;

                int64_t eventTime = recordingSampleCounter;

                if (isCountingIn && captureNextEventAtZero) {
                    eventTime = 0;
                    captureNextEventAtZero = false;
                }

                eventBuffer[writeIndex] = {padIndex, eventTime};
                eventFifo.finishedWrite(write.blockSize1);
            }
        }

        activePadIndex = padIndex;
        padFlashCountdown = 3; // 3 ticks @ 30Hz = ~100ms

        repaint();

        return true;
    }

    return false;
}

void MainComponent::timerCallback() {
    bool shouldRepaint = false;

    if (padFlashCountdown > 0) {
        padFlashCountdown--;

        if (padFlashCountdown == 0) {
            activePadIndex = -1;
            shouldRepaint = true;
        }
    }

    if (needsRepaint || isCountingIn) {
        needsRepaint = false;
        shouldRepaint = true;
    }

    if (shouldRepaint) {
        repaint();
    }
}

void MainComponent::buttonClicked(juce::Button *button) {
    if (button == &settingsButton) {
        audioSettingsSelector.reset(new juce::AudioDeviceSelectorComponent(
            deviceManager, 0, 0, 1, 2, false, false, true, false));

        audioSettingsSelector->setSize(500, 270);

        juce::DialogWindow::LaunchOptions options;

        options.content.setNonOwned(audioSettingsSelector.get());
        options.dialogTitle = "Audio Settings";
        options.componentToCentreAround = this;
        options.resizable = false;

        options.launchAsync();
    } else if (button == &recordButton) {
        if (isRecording)
            stopRecording();
        else
            startRecording();
    }
}

void MainComponent::comboBoxChanged(juce::ComboBox *box) {}

void MainComponent::startRecording() {
    if (fileBuffer.getNumSamples() == 0)
        return;

    currentBpm = bpmEditor.getText().getDoubleValue();
    if (currentBpm < 30)
        currentBpm = 90.0;

    int bars = 2;

    if (barsBox.getSelectedId() == 2)
        bars = 4;
    if (barsBox.getSelectedId() == 3)
        bars = 8;

    currentBarCount = bars;

    double secondsPerBeat = 60.0 / currentBpm;
    double totalSeconds = bars * 4 * secondsPerBeat; // 4/4 time signature

    totalRecordingLengthSamples = (int64_t)(totalSeconds * sampleRate);
    samplesPerBeat = (int)(secondsPerBeat * sampleRate);

    recordedEvents.clear();
    recordedEvents.reserve(256);
    recordingSampleCounter = 0;

    isCountingIn = true;
    isRecording = false;
    countInBeats = 0;
    samplesSinceLastClick = samplesPerBeat;

    recordButton.setButtonText("Count-in...");
    grabKeyboardFocus();
}

void MainComponent::stopRecording() {
    isRecording = false;
    recordButton.setButtonText("Record");

    exportRecording();
}

void MainComponent::addMetronomeToBuffer(
    const juce::AudioSourceChannelInfo &bufferToFill) {
    if (!isRecording && !isCountingIn)
        return;

    auto *left =
        bufferToFill.buffer->getWritePointer(0, bufferToFill.startSample);
    auto *right =
        bufferToFill.buffer->getWritePointer(1, bufferToFill.startSample);

    for (int sample = 0; sample < bufferToFill.numSamples; ++sample) {
        // metronome click
        if (samplesSinceLastClick >= samplesPerBeat) {
            samplesSinceLastClick = 0;

            if (isCountingIn) {
                countInBeats++;

                if (countInBeats > 4) {
                    isCountingIn = false;
                    isRecording = true;

                    recordingSampleCounter = 0;

                    captureNextEventAtZero = true;
                    pendingStartRecordingUI = true;
                } else {
                    needsRepaint = true;
                }
            }
        }

        // synthesize a "beep" for the first 2000 samples of a beat
        if (samplesSinceLastClick < 2000) {
            float clickLevel = 0.7f;

            // pitch: high (1200Hz) for count in, low (880Hz) for recording
            float freqHz = isCountingIn ? 1200.0f : 880.0f;
            float phase = 2.0f * juce::MathConstants<float>::pi * freqHz *
                          samplesSinceLastClick / sampleRate;

            float currentSample = std::sin(phase) * clickLevel;

            // fade out
            currentSample *= (1.0f - (samplesSinceLastClick / 2000.0f));

            left[sample] += currentSample;
            right[sample] += currentSample;
        }

        samplesSinceLastClick++;

        // auto-stop check
        if (isRecording) {
            recordingSampleCounter++;

            if (recordingSampleCounter >= totalRecordingLengthSamples) {
                isRecording = false;

                // stop safely on main thread
                juce::MessageManager::callAsync([this]() { stopRecording(); });
                break;
            }
        }
    }

    while (eventFifo.getNumReady() > 0) {
        auto read = eventFifo.read(1);

        if (read.blockSize1 > 0) {
            const int readIndex = read.startIndex1;

            recordedEvents.push_back(eventBuffer[readIndex]);

            eventFifo.finishedRead(read.blockSize1);
        }
    }
}

void MainComponent::exportRecording() {
    if (recordedEvents.empty())
        return;

    fileChooser = std::make_unique<juce::FileChooser>(
        "Save Recording",
        juce::File::getSpecialLocation(juce::File::userHomeDirectory), "*.wav");

    fileChooser->launchAsync(juce::FileBrowserComponent::saveMode |
                                 juce::FileBrowserComponent::canSelectFiles,

                             [this](const juce::FileChooser &fc) {
                                 auto fileToSave = fc.getResult();

                                 if (fileToSave == juce::File{})
                                     return;

                                 writeRecordingToFile(fileToSave);
                             });
}

void MainComponent::writeRecordingToFile(const juce::File &fileToSave) {
    juce::AudioBuffer<float> outputBuffer(2, (int)totalRecordingLengthSamples);
    outputBuffer.clear();

    std::sort(recordedEvents.begin(), recordedEvents.end(),
              [](const RecordedEvent &a, const RecordedEvent &b) {
                  return a.timeInSamples < b.timeInSamples;
              });

    for (size_t i = 0; i < recordedEvents.size(); ++i) {
        auto &event = recordedEvents[i];

        int64_t sourceStart =
            (int64_t)(chopPositions[event.padIndex] * sampleRate);
        int64_t destStart = event.timeInSamples;

        int64_t limit;
        if (i < recordedEvents.size() - 1) {
            limit = recordedEvents[i + 1].timeInSamples; // stop at next chop
        } else {
            limit = totalRecordingLengthSamples; // last note plays to end
        }

        // chop duration
        int64_t duration = limit - destStart;

        // clamp to source sample duration
        if (sourceStart + duration > fileBuffer.getNumSamples()) {
            duration = fileBuffer.getNumSamples() - sourceStart;
        }

        if (duration > 0) {
            outputBuffer.addFrom(0, (int)destStart, fileBuffer, 0,
                                 (int)sourceStart, (int)duration);
            outputBuffer.addFrom(1, (int)destStart, fileBuffer, 1,
                                 (int)sourceStart, (int)duration);

            int fadeLen = juce::jmin((int)duration, (int)(0.005 * sampleRate));
            outputBuffer.applyGainRamp((int)destStart + (int)duration - fadeLen,
                                       fadeLen, 1.0f, 0.0f);
        }
    }

    writeWav(outputBuffer, fileToSave);
}

void MainComponent::writeWav(juce::AudioBuffer<float> &buffer,
                             const juce::File &fileToSave) {
    juce::WavAudioFormat wavFormat;

    juce::AudioFormatWriterOptions options;

    options = options.withSampleRate(sampleRate);
    options = options.withNumChannels(2);
    options = options.withBitsPerSample(16);

    auto fileStream = std::make_unique<juce::FileOutputStream>(fileToSave);
    if (!fileStream->openedOk())
        return;

    std::unique_ptr<juce::OutputStream> stream = std::move(fileStream);

    auto writer = std::unique_ptr<juce::AudioFormatWriter>(
        wavFormat.createWriterFor(stream, options));

    if (writer)
        writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
}
