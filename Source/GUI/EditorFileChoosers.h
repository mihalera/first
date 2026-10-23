#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace j37::gui {
/** Owns one async chooser and guards its callback against editor destruction. */
template <typename Editor>
class SafeFileChooser final {
  public:
    template <typename Callback>
    bool launch(Editor &editor,
                const juce::String &title,
                const juce::File &initialLocation,
                const juce::String &wildcard,
                int flags,
                Callback &&callback) {
        if (chooser != nullptr)
            return false;

        chooser = std::make_shared<juce::FileChooser>(title, initialLocation, wildcard);
        const auto retainedChooser = chooser;
        const juce::Component::SafePointer<Editor> safeEditor(&editor);
        retainedChooser->launchAsync(
            flags, [this, safeEditor, retainedChooser, callback = std::forward<Callback>(callback)](
                       const juce::FileChooser &completedChooser) mutable {
                if (chooser != retainedChooser)
                    return;
                chooser.reset();
                if (safeEditor != nullptr)
                    callback(*safeEditor, completedChooser);
            });
        return true;
    }

    void cancel() noexcept { chooser.reset(); }
    bool isActive() const noexcept { return chooser != nullptr; }
    bool isCurrent (const std::shared_ptr<juce::FileChooser>& candidate) const noexcept
    {
        return chooser == candidate;
    }

  private:
    std::shared_ptr<juce::FileChooser> chooser;
};
} // namespace j37::gui
