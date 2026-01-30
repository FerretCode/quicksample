#include "MainComponent.h"
#include <JuceHeader.h>

class SimpleSamplerApplication : public juce::JUCEApplication {
  public:
    SimpleSamplerApplication() {}
    const juce::String getApplicationName() override { return "quicksample"; }
    const juce::String getApplicationVersion() override { return "1.0.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String &) override {
        mainWindow.reset(new MainWindow(getApplicationName()));
    }

    void shutdown() override {
        mainWindow = nullptr; // (deletes our window)
    }

    void systemRequestedQuit() override { quit(); }

    class MainWindow : public juce::DocumentWindow {
      public:
        MainWindow(juce::String name)
            : DocumentWindow(
                  name,
                  juce::Desktop::getInstance()
                      .getDefaultLookAndFeel()
                      .findColour(juce::ResizableWindow::backgroundColourId),
                  DocumentWindow::allButtons) {
            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(), true);
            setResizable(true, false);
            centreWithSize(800, 600);
            setVisible(true);
        }

        void closeButtonPressed() override {
            JUCEApplication::getInstance()->systemRequestedQuit();
        }

      private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
    };

  private:
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(SimpleSamplerApplication)
