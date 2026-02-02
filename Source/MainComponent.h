#pragma once
#include "juce_audio_basics/juce_audio_basics.h"
#include "juce_audio_utils/juce_audio_utils.h"
#include "juce_core/juce_core.h"
#include "juce_gui_basics/juce_gui_basics.h"
#include <JuceHeader.h>
#include <memory>

struct RecordedEvent {
    int padIndex;
    int64_t
        timeInSamples; // when the user  triggered this event relative to start
};

class MainComponent : public juce::AudioAppComponent,
                      public juce::FileDragAndDropTarget,
                      public juce::ChangeListener,
                      public juce::Button::Listener,
                      public juce::ComboBox::Listener,
                      private juce::Timer {
  public:
    MainComponent();
    ~MainComponent() override;

    // audio lifecycle
    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock(
        const juce::AudioSourceChannelInfo &bufferToFill) override;
    void releaseResources() override;

    // drawing & interaction
    void paint(juce::Graphics &g) override;
    void resized() override;

    // drag and drop
    bool isInterestedInFileDrag(const juce::StringArray &files) override;
    void filesDropped(const juce::StringArray &files, int x, int y) override;

    // interaction listeners
    bool keyPressed(const juce::KeyPress &key) override;
    void buttonClicked(juce::Button *button) override;
    void comboBoxChanged(juce::ComboBox *comboBox) override;

    // handlers for chop adjustment
    void mouseDown(const juce::MouseEvent &e) override;
    void mouseDrag(const juce::MouseEvent &e) override;
    void mouseUp(const juce::MouseEvent &e) override;
    void mouseWheelMove(const juce::MouseEvent &e,
                        const juce::MouseWheelDetails &wheel) override;

  private:
    // audio engine
    juce::AudioFormatManager formatManager;
    juce::AudioTransportSource transportSource;
    juce::AudioBuffer<float> fileBuffer;
    std::unique_ptr<juce::MemoryAudioSource> memorySource;
    juce::CriticalSection wrapperLock;

    // waveform cache
    juce::AudioThumbnailCache thumbnailCache{5}; // cache 5 files
    juce::AudioThumbnail thumbnail;

    // ui elements
    juce::TextButton settingsButton{"Audio Settings"};
    std::unique_ptr<juce::AudioDeviceSelectorComponent> audioSettingsSelector;
    juce::DialogWindow::LaunchOptions settingsWindow;

    juce::TextButton recordButton{"Record"};
    juce::Label bpmLabel{"bpmLabel", "BPM:"};
    juce::TextEditor bpmEditor;
    juce::Label barsLabel{"barsLabel", "Bars:"};
    juce::ComboBox barsBox;

    std::unique_ptr<juce::FileChooser> fileChooser;

    // state
    std::vector<double> chopPositions;

    int activePadIndex = -1;
    int selectedChopIndex = -1;

    double visibleStartTime = 0.0;
    double visibleDuration = 0.0;

    bool isDraggingChop = false;

    bool isRecording = false;
    double currentBpm = 90;
    int currentBarCount = 4;

    bool needsRepaint = false;
    int padFlashCountdown = 0;

    bool captureNextEventAtZero = false;

    int64_t recordingSampleCounter =
        0; // how many samples since recording started
    int64_t totalRecordingLengthSamples = 0; // when to auto-stop
    std::vector<RecordedEvent> recordedEvents;

    std::vector<RecordedEvent> eventBuffer;
    juce::AbstractFifo eventFifo{256};

    juce::LookAndFeel_V4 modernLookAndFeel;

    // metronome state
    double sampleRate = 44100.0;
    int samplesPerBeat = 0;
    int samplesSinceLastClick = 0;

    bool isCountingIn = false;
    int countInBeats = 0;

    bool pendingStartRecordingUI = true;

    // private methods
    void loadFile(const juce::File &file);
    void changeListenerCallback(juce::ChangeBroadcaster *source) override;
    void timerCallback() override;

    // recording
    void startRecording();
    void stopRecording();
    void exportRecording(); // handle file picker
    void writeRecordingToFile(
        const juce::File
            &fileToSave); // helper for pasting chops into audio buffer
    void writeWav(juce::AudioBuffer<float> &buffer,
                  const juce::File &fileToSave);
    void addMetronomeToBuffer(const juce::AudioSourceChannelInfo &bufferToFill);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
