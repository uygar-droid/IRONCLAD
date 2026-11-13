#pragma once

// Small editor controls used by the temporary (Milestones 1-5) interface.

#if IPLUG_EDITOR
#include <functional>
#include <string>

#include "IControl.h"

namespace ironclad_ui
{
using namespace iplug;
using namespace iplug::igraphics;

/// A text field that acts like a button (click -> callback) and can ask for a line of text
/// in place (used for the preset name: click opens the browser, Save As / Rename edit it).
class ClickTextControl : public IControl
{
public:
  ClickTextControl(const IRECT& bounds, const IText& text, IColor background, std::function<void()> onClick)
  : IControl(bounds)
  , mBackground(background)
  , mOnClick(std::move(onClick))
  {
    mText = text;
  }

  void SetLabel(const std::string& label)
  {
    if (label != mLabel)
    {
      mLabel = label;
      SetDirty(false);
    }
  }

  void Draw(IGraphics& g) override
  {
    g.FillRoundRect(mMouseIsOver ? mBackground.WithContrast(0.08f) : mBackground, mRECT, 4.f);
    g.DrawText(mText, mLabel.c_str(), mRECT.GetPadded(-8.f, 0.f, -8.f, 0.f));
  }

  void OnMouseDown(float, float, const IMouseMod&) override
  {
    if (mOnClick)
      mOnClick();
  }

  /// Opens an in-place text editor; `done` receives the text (not called if cancelled).
  void PromptText(const std::string& initial, std::function<void(const std::string&)> done)
  {
    mOnText = std::move(done);
    if (IGraphics* g = GetUI())
      g->CreateTextEntry(*this, mText.WithAlign(EAlign::Near), mRECT.GetPadded(-2.f), initial.c_str());
  }

  void OnTextEntryCompletion(const char* str, int) override
  {
    auto done = std::move(mOnText);
    mOnText = nullptr;
    if (done && str != nullptr)
      done(str);
  }

private:
  IColor mBackground;
  std::function<void()> mOnClick;
  std::function<void(const std::string&)> mOnText;
  std::string mLabel;
};

} // namespace ironclad_ui
#endif
