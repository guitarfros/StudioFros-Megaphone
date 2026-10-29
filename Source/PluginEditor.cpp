// StudioFros - Megaphone
// Copyright (C) GuitarFros. Licensed under the GNU AGPLv3 (see LICENSE).
#include "PluginEditor.h"

#include <array>
#include <cmath>

#include "PanelLayout.h"
#include "StudioAssets.h"

using namespace studiofros;

namespace
{
    const juce::Colour kCream (0xffe8e2cf);
    const juce::Colour kInk   (0xff141a0c);     // LCD dots
    const juce::Colour kRust  (0xffc2381f);
    const juce::Colour kDark  (0xff17191b);

    // The slider area of a knob is kHitFactor times its cap radius (cap + scale ticks).
    constexpr float kHitFactor = 3.8f;

    juce::Image loadImage (const void* data, int size)
    {
        return juce::ImageFileFormat::loadFrom (data, (size_t) size);
    }

    juce::Rectangle<int> toRect (const panel::Rect& r) { return { r.x, r.y, r.w, r.h }; }

    //==========================================================================
    // Writes text on the LCD as a dot matrix: the text is rasterised into a tiny
    // bitmap (one pixel = one dot) and every lit pixel is drawn as a round dot.
    void drawDotText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                      int rows, float fontHeight, float maxPitch, float minPitch, juce::Colour colour)
    {
        if (text.isEmpty() || area.isEmpty())
            return;

        juce::Font font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), fontHeight, juce::Font::bold));

        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText (font, text, 0.0f, 0.0f);
        const int cols = juce::jmax (1, (int) std::ceil (glyphs.getBoundingBox (0, -1, true).getRight()) + 2);

        juce::Image raster (juce::Image::SingleChannel, cols, rows, true);
        {
            juce::Graphics rg (raster);
            rg.setColour (juce::Colours::white);
            rg.setFont (font);
            rg.drawText (text, juce::Rectangle<int> (0, 0, cols, rows), juce::Justification::centredLeft, false);
        }

        float pitch = juce::jmin (maxPitch, area.getWidth() / (float) cols, area.getHeight() / (float) rows);
        pitch = juce::jmax (minPitch, pitch);

        const float dot = pitch * 0.84f;
        const float x0 = area.getCentreX() - (float) cols * pitch * 0.5f;
        const float y0 = area.getCentreY() - (float) rows * pitch * 0.5f;

        juce::Image::BitmapData bits (raster, juce::Image::BitmapData::readOnly);
        for (int y = 0; y < rows; ++y)
        {
            for (int x = 0; x < cols; ++x)
            {
                const float a = bits.getPixelColour (x, y).getFloatAlpha();
                if (a < 0.25f)
                    continue;

                g.setColour (colour.withMultipliedAlpha (juce::jlimit (0.0f, 1.0f, (a - 0.15f) / 0.6f)));
                g.fillEllipse (x0 + (float) x * pitch + (pitch - dot) * 0.5f,
                               y0 + (float) y * pitch + (pitch - dot) * 0.5f, dot, dot);
            }
        }
    }

    //==========================================================================
    class PanelLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        PanelLookAndFeel()
        {
            setColour (juce::PopupMenu::backgroundColourId, kDark);
            setColour (juce::PopupMenu::textColourId, kCream);
            setColour (juce::PopupMenu::headerTextColourId, kRust.brighter (0.4f));
            setColour (juce::PopupMenu::highlightedBackgroundColourId, kRust);
            setColour (juce::PopupMenu::highlightedTextColourId, kCream);
        }

        // Only the pointer is drawn here: cap, scale and labels are part of the panel picture.
        void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                               float sliderPos, float startAngle, float endAngle, juce::Slider&) override
        {
            const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
            const auto c = bounds.getCentre();
            const float capR = (float) juce::jmin (width, height) / kHitFactor;
            const float angle = startAngle + sliderPos * (endAngle - startAngle);
            const juce::Point<float> dir (std::sin (angle), -std::cos (angle));

            const auto p0 = c + dir * (capR * 0.06f);
            const auto p1 = c + dir * (capR * 0.90f);
            const float w = capR * 0.12f;

            juce::Path line;
            line.startNewSubPath (p0);
            line.lineTo (p1);

            const juce::PathStrokeType dark (w + capR * 0.05f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
            const juce::PathStrokeType light (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.strokePath (line, dark, juce::AffineTransform::translation (capR * 0.03f, capR * 0.05f));
            g.setColour (juce::Colour (0xffe9e4cf).withAlpha (0.96f));
            g.strokePath (line, light);
        }
    };

    //==========================================================================
    // Invisible button over a part of the panel. When pressed, the picture of
    // that part is drawn slightly smaller and darker, as if it sank in.
    class PressButton : public juce::Button
    {
    public:
        PressButton (const juce::Image& panelPicture, juce::Rectangle<int> area, bool isCircle)
            : juce::Button (juce::String()), picture (panelPicture), source (area), circle (isCircle)
        {
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }

        bool hitTest (int x, int y) override
        {
            if (! circle)
                return true;

            const float r = (float) getWidth() * 0.5f;
            return std::hypot ((float) x - r, (float) y - r) <= r;
        }

        void paintButton (juce::Graphics& g, bool, bool isDown) override
        {
            const bool dim = ! isEnabled();
            if (! isDown && ! dim)
                return;

            const auto b = getLocalBounds().toFloat();
            const auto inner = circle ? b.withSizeKeepingCentre (b.getWidth() * 0.78f, b.getHeight() * 0.78f)
                                      : b.reduced (b.getWidth() * 0.14f, b.getHeight() * 0.12f);

            juce::Path shape;
            if (circle) shape.addEllipse (inner);
            else        shape.addRoundedRectangle (inner, 6.0f);

            juce::Graphics::ScopedSaveState state (g);
            g.reduceClipRegion (shape, {});

            if (isDown)
            {
                g.setColour (juce::Colours::black.withAlpha (0.6f));
                g.fillPath (shape);

                const auto srcRect = inner.translated ((float) source.getX(), (float) source.getY()).toNearestInt();
                g.drawImage (picture.getClippedImage (srcRect), inner.reduced (inner.getWidth() * 0.04f));
            }

            g.setColour (juce::Colours::black.withAlpha (isDown ? 0.28f : 0.5f));
            g.fillPath (shape);
        }

    private:
        juce::Image picture;
        juce::Rectangle<int> source;
        bool circle;
    };

    //==========================================================================
    // A lever drawn from one picture per position, linked to a parameter.
    // Pictures are ordered like the parameter values: state 0, 1 (, 2).
    class SpriteSwitch : public juce::Component
    {
    public:
        SpriteSwitch (juce::RangedAudioParameter& p, std::vector<juce::Image> pictures)
            : sprites (std::move (pictures)),
              attachment (p, [this] (float v) { setState ((int) std::lround (v)); })
        {
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            attachment.sendInitialUpdate();
        }

        void paint (juce::Graphics& g) override
        {
            if (state >= 0 && state < (int) sprites.size())
                g.drawImage (sprites[(size_t) state], getLocalBounds().toFloat());
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            const int n = (int) sprites.size();
            if (n == 2)
                commit (1 - state);
            else
                commit (juce::jlimit (0, n - 1, state + (e.y < getHeight() / 2 ? -1 : 1)));
        }

        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
        {
            if (sprites.size() < 3 || wheel.deltaY == 0.0f)
                return;

            commit (juce::jlimit (0, (int) sprites.size() - 1, state + (wheel.deltaY > 0.0f ? -1 : 1)));
        }

    private:
        void setState (int s)
        {
            s = juce::jlimit (0, (int) sprites.size() - 1, s);
            if (s != state)
            {
                state = s;
                repaint();
            }
        }

        void commit (int s)
        {
            s = juce::jlimit (0, (int) sprites.size() - 1, s);
            attachment.setValueAsCompleteGesture ((float) s);
            setState (s);
        }

        std::vector<juce::Image> sprites;
        int state = -1;
        juce::ParameterAttachment attachment;
    };

    //==========================================================================
    class LedLamp : public juce::Component
    {
    public:
        // lit when the parameter value is 0 (effect active = bypass off)
        LedLamp (juce::RangedAudioParameter& bypass, juce::Image onImage, juce::Image offImage)
            : on (std::move (onImage)), off (std::move (offImage)),
              attachment (bypass, [this] (float v)
                          {
                              const bool nowLit = v < 0.5f;
                              if (nowLit != lit) { lit = nowLit; repaint(); }
                          })
        {
            setInterceptsMouseClicks (false, false);
            attachment.sendInitialUpdate();
        }

        void paint (juce::Graphics& g) override
        {
            g.drawImage (lit ? on : off, getLocalBounds().toFloat());
        }

    private:
        juce::Image on, off;
        bool lit = true;
        juce::ParameterAttachment attachment;
    };

    //==========================================================================
    class LcdDisplay : public juce::Component
    {
    public:
        std::function<void()> onClick;

        LcdDisplay() { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

        void setText (const juce::String& newMain, const juce::String& newSub)
        {
            if (newMain != mainText || newSub != subText)
            {
                mainText = newMain;
                subText = newSub;
                repaint();
            }
        }

        void paint (juce::Graphics& g) override
        {
            auto b = getLocalBounds().toFloat();
            const auto mainArea = b.removeFromTop (b.getHeight() * 0.64f);

            drawDotText (g, mainText, mainArea, 16, 13.0f, 4.4f, 1.4f, kInk.withAlpha (0.92f));
            drawDotText (g, subText, b, 12, 10.0f, 3.1f, 1.4f, kInk.withAlpha (0.62f));
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            if (e.mouseWasClicked() && onClick)
                onClick();
        }

    private:
        juce::String mainText, subText;
    };
}

//==============================================================================
class MegaphonePanel : public juce::Component,
                       private juce::Timer
{
public:
    explicit MegaphonePanel (MegaphoneProcessor& p)
        : processor (p),
          background (loadImage (StudioAssets::panel_jpg, StudioAssets::panel_jpgSize)),
          saveButton   (background, toRect (panel::kBtnSave),   false),
          saveAsButton (background, toRect (panel::kBtnSaveAs), false),
          deleteButton (background, toRect (panel::kBtnDelete), false),
          initButton   (background, toRect (panel::kBtnInit),   false),
          prevButton   (background, toRect (panel::kBtnPrev),   false),
          nextButton   (background, toRect (panel::kBtnNext),   false),
          triggerButton (background, triggerArea(), true)
    {
        setLookAndFeel (&laf);

        // ---- knobs ------------------------------------------------------------
        constexpr float deg = juce::MathConstants<float>::pi / 180.0f;

        for (size_t i = 0; i < knobs.size(); ++i)
        {
            auto& k = knobs[i];
            const auto& pos = panel::kKnobs[i];
            const auto& spec = kParamSpecs[pos.param];

            k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            k.slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
            k.slider.setRotaryParameters (-150.0f * deg, 150.0f * deg, true);
            k.slider.setMouseDragSensitivity (240);
            k.slider.setMouseCursor (juce::MouseCursor::PointingHandCursor);
            addAndMakeVisible (k.slider);

            k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, spec.id, k.slider);
            k.slider.setDoubleClickReturnValue (true, (double) spec.def);

            const int index = (int) i;
            k.slider.onValueChange = [this, index]
            {
                if (knobs[(size_t) index].slider.isMouseOverOrDragging())
                    showReadout (index);
            };
        }

        // ---- switches (one picture per lever position) ---------------------------
        auto sprite = [] (const void* d, int n) { return loadImage (d, n); };

        compSwitch = std::make_unique<SpriteSwitch> (*processor.apvts.getParameter (kParamSpecs[pComp].id),
            std::vector<juce::Image> { sprite (StudioAssets::comp_off_png, StudioAssets::comp_off_pngSize),
                                       sprite (StudioAssets::comp_on_png,  StudioAssets::comp_on_pngSize) });

        modeSwitch = std::make_unique<SpriteSwitch> (*processor.apvts.getParameter (kParamSpecs[pMode].id),
            std::vector<juce::Image> { sprite (StudioAssets::mode_1_png, StudioAssets::mode_1_pngSize),
                                       sprite (StudioAssets::mode_2_png, StudioAssets::mode_2_pngSize) });

        noiseSwitch = std::make_unique<SpriteSwitch> (*processor.apvts.getParameter (kParamSpecs[pNoiseSource].id),
            std::vector<juce::Image> { sprite (StudioAssets::noise_click_png, StudioAssets::noise_click_pngSize),
                                       sprite (StudioAssets::noise_both_png,  StudioAssets::noise_both_pngSize),
                                       sprite (StudioAssets::noise_burst_png, StudioAssets::noise_burst_pngSize) });

        bypassSwitch = std::make_unique<SpriteSwitch> (*processor.apvts.getParameter (kBypassId),
            std::vector<juce::Image> { sprite (StudioAssets::bypass_active_png, StudioAssets::bypass_active_pngSize),
                                       sprite (StudioAssets::bypass_off_png,    StudioAssets::bypass_off_pngSize) });

        led = std::make_unique<LedLamp> (*processor.apvts.getParameter (kBypassId),
                                         sprite (StudioAssets::led_on_png,  StudioAssets::led_on_pngSize),
                                         sprite (StudioAssets::led_off_png, StudioAssets::led_off_pngSize));

        addAndMakeVisible (*compSwitch);
        addAndMakeVisible (*modeSwitch);
        addAndMakeVisible (*noiseSwitch);
        addAndMakeVisible (*bypassSwitch);
        addAndMakeVisible (*led);

        // ---- trigger: a momentary push button -------------------------------------
        triggerButton.onStateChange = [this]
        {
            auto* param = processor.apvts.getParameter (kTriggerId);
            if (param == nullptr)
                return;

            const bool down = triggerButton.isDown();
            if (down == triggerHeld)
                return;

            triggerHeld = down;
            if (down) param->beginChangeGesture();
            param->setValueNotifyingHost (down ? 1.0f : 0.0f);
            if (! down) param->endChangeGesture();
        };
        addAndMakeVisible (triggerButton);

        // ---- presets -----------------------------------------------------------------
        lcd.onClick = [this] { showPresetMenu(); };
        addAndMakeVisible (lcd);

        prevButton.onClick = [this] { stepPreset (-1); };
        nextButton.onClick = [this] { stepPreset (1); };
        saveButton.onClick = [this] { savePressed(); };
        saveAsButton.onClick = [this] { beginNameEntry(); };
        deleteButton.onClick = [this] { deletePressed(); };
        initButton.onClick = [this]
        {
            cancelNameEntry();
            processor.presets.loadInit();
            refresh();
        };

        for (auto* b : { &prevButton, &nextButton, &saveButton, &saveAsButton, &deleteButton, &initButton })
            addAndMakeVisible (*b);

        nameEditor.setMultiLine (false);
        nameEditor.setReturnKeyStartsNewLine (false);
        nameEditor.setInputRestrictions (40);
        nameEditor.setJustification (juce::Justification::centred);
        nameEditor.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 44.0f, juce::Font::bold)));
        nameEditor.setTextToShowWhenEmpty ("NAME + ENTER", kInk.withAlpha (0.4f));
        nameEditor.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xffa9b383));
        nameEditor.setColour (juce::TextEditor::textColourId, kInk);
        nameEditor.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        nameEditor.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
        nameEditor.setColour (juce::TextEditor::highlightColourId, kInk.withAlpha (0.25f));
        nameEditor.setColour (juce::TextEditor::highlightedTextColourId, kInk);
        nameEditor.setColour (juce::CaretComponent::caretColourId, kInk);
        nameEditor.onReturnKey = [this] { commitNameEntry(); };
        nameEditor.onEscapeKey = [this] { cancelNameEntry(); };
        addChildComponent (nameEditor);

        setSize (panel::kWidth, panel::kHeight);
        refresh();
        startTimerHz (15);
    }

    ~MegaphonePanel() override
    {
        stopTimer();
        setLookAndFeel (nullptr);
    }

    void paint (juce::Graphics& g) override
    {
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (background, getLocalBounds().toFloat());
    }

    void resized() override
    {
        for (size_t i = 0; i < knobs.size(); ++i)
        {
            const auto& k = panel::kKnobs[i];
            const int half = (int) std::lround (k.capRadius * kHitFactor * 0.5f);
            knobs[i].slider.setBounds ((int) std::lround (k.cx) - half, (int) std::lround (k.cy) - half, 2 * half, 2 * half);
        }

        compSwitch->setBounds (toRect (panel::kSprComp));
        modeSwitch->setBounds (toRect (panel::kSprMode));
        noiseSwitch->setBounds (toRect (panel::kSprNoise));
        bypassSwitch->setBounds (toRect (panel::kSprBypass));
        led->setBounds (toRect (panel::kSprLed));

        triggerButton.setBounds (triggerArea());

        prevButton.setBounds (toRect (panel::kBtnPrev));
        nextButton.setBounds (toRect (panel::kBtnNext));
        saveButton.setBounds (toRect (panel::kBtnSave));
        saveAsButton.setBounds (toRect (panel::kBtnSaveAs));
        deleteButton.setBounds (toRect (panel::kBtnDelete));
        initButton.setBounds (toRect (panel::kBtnInit));

        lcd.setBounds (toRect (panel::kLcdText));
        nameEditor.setBounds (toRect (panel::kLcdText).reduced (0, 12));
    }

private:
    struct Knob
    {
        juce::Slider slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    static juce::Rectangle<int> triggerArea()
    {
        const auto& t = panel::kTrigger;
        return { (int) std::lround (t.cx - t.r), (int) std::lround (t.cy - t.r), (int) std::lround (2 * t.r), (int) std::lround (2 * t.r) };
    }

    void timerCallback() override { refresh(); }

    // Shows "DRIVE  55 %" on the LCD for a moment while a knob is being turned.
    void showReadout (int knobIndex)
    {
        const auto& pos = panel::kKnobs[(size_t) knobIndex];
        const auto& spec = kParamSpecs[pos.param];
        const double v = knobs[(size_t) knobIndex].slider.getValue();

        // At most two decimals; trailing zeros are dropped ("55", "55.5", "55.25").
        juce::String value = juce::String (std::round (v * 100.0) / 100.0, 2);
        if (value.containsChar ('.'))
            value = value.trimCharactersAtEnd ("0").trimCharactersAtEnd (".");
        if (value == "-0")
            value = "0";
        if (pos.param == pOutput && v > 0.0)
            value = "+" + value;

        readout = juce::String (pos.label) + "  " + value + spec.unit;
        readoutUntil = juce::Time::getMillisecondCounter() + 1400;
        refresh();
    }

    void refresh()
    {
        auto& pm = processor.presets;
        const bool factory = pm.isCurrentFactory();
        const auto now = juce::Time::getMillisecondCounter();

        deleteButton.setEnabled (! factory);

        if (deleteArmed && (juce::int32) (now - deleteArmedUntil) > 0)
            deleteArmed = false;

        const bool readoutActive = readoutUntil != 0 && (juce::int32) (readoutUntil - now) > 0;

        if (deleteArmed)
            lcd.setText ("DELETE?  CLICK AGAIN", {});
        else if (readoutActive)
            lcd.setText (readout, {});
        else
            lcd.setText (pm.getCurrentName() + (pm.isModified() ? " *" : ""), factory ? "FACTORY" : "USER");
    }

    void showPresetMenu()
    {
        if (nameEditor.isVisible())
            return;

        const auto list = processor.presets.getAllPresets();
        juce::PopupMenu menu;
        bool userHeaderAdded = false;

        menu.addSectionHeader ("Factory");
        for (size_t i = 0; i < list.size(); ++i)
        {
            if (! list[i].factory && ! userHeaderAdded)
            {
                menu.addSeparator();
                menu.addSectionHeader ("User");
                userHeaderAdded = true;
            }

            const bool isCurrent = list[i].factory == processor.presets.isCurrentFactory()
                                   && list[i].name == processor.presets.getCurrentName();
            menu.addItem ((int) i + 1, list[i].name, true, isCurrent);
        }

        juce::Component::SafePointer<MegaphonePanel> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options()
                                .withTargetComponent (&lcd)
                                .withMinimumWidth (lcd.getWidth()),
                            [safe, list] (int result)
                            {
                                if (safe.getComponent() == nullptr || result <= 0 || result > (int) list.size())
                                    return;
                                safe->processor.presets.load (list[(size_t) result - 1]);
                                safe->refresh();
                            });
    }

    void stepPreset (int direction)
    {
        cancelNameEntry();

        const auto list = processor.presets.getAllPresets();
        if (list.empty())
            return;

        int current = -1;
        for (size_t i = 0; i < list.size(); ++i)
            if (list[i].factory == processor.presets.isCurrentFactory() && list[i].name == processor.presets.getCurrentName())
                current = (int) i;

        const int n = (int) list.size();
        const int next = current < 0 ? 0 : (current + direction + n) % n;
        processor.presets.load (list[(size_t) next]);
        refresh();
    }

    void savePressed()
    {
        if (processor.presets.isCurrentFactory())
        {
            beginNameEntry();     // a factory preset can't be overwritten: ask for a name
            return;
        }

        processor.presets.saveUser (processor.presets.getCurrentName());
        refresh();
    }

    void beginNameEntry()
    {
        if (nameEditor.isVisible())
        {
            cancelNameEntry();
            return;
        }

        nameEditor.setText (processor.presets.isCurrentFactory() ? juce::String() : processor.presets.getCurrentName(), false);
        nameEditor.setVisible (true);
        nameEditor.toFront (true);
        nameEditor.grabKeyboardFocus();
        nameEditor.selectAll();
    }

    void commitNameEntry()
    {
        const auto name = nameEditor.getText();
        nameEditor.setVisible (false);

        if (PresetManager::sanitiseName (name).isNotEmpty())
            processor.presets.saveUser (name);

        refresh();
    }

    void cancelNameEntry()
    {
        nameEditor.setVisible (false);
    }

    void deletePressed()
    {
        if (processor.presets.isCurrentFactory())
            return;

        if (! deleteArmed)
        {
            deleteArmed = true;
            deleteArmedUntil = juce::Time::getMillisecondCounter() + 3000;
            refresh();
            return;
        }

        deleteArmed = false;
        processor.presets.deleteUser (processor.presets.getCurrentName());
        refresh();
    }

    //--------------------------------------------------------------------------
    MegaphoneProcessor& processor;
    PanelLookAndFeel laf;
    juce::Image background;

    std::array<Knob, 8> knobs;

    std::unique_ptr<SpriteSwitch> compSwitch, modeSwitch, noiseSwitch, bypassSwitch;
    std::unique_ptr<LedLamp> led;

    PressButton saveButton, saveAsButton, deleteButton, initButton, prevButton, nextButton, triggerButton;
    bool triggerHeld = false;

    LcdDisplay lcd;
    juce::TextEditor nameEditor;

    juce::String readout;
    juce::uint32 readoutUntil = 0;
    bool deleteArmed = false;
    juce::uint32 deleteArmedUntil = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MegaphonePanel)
};

//==============================================================================
MegaphoneEditor::MegaphoneEditor (MegaphoneProcessor& p)
    : AudioProcessorEditor (&p),
      panel (std::make_unique<MegaphonePanel> (p))
{
    addAndMakeVisible (*panel);

    setResizeLimits (panel::kWidth / 4, panel::kHeight / 4, panel::kWidth, panel::kHeight);
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) panel::kWidth / (double) panel::kHeight);
    setSize (panel::kWidth / 2, panel::kHeight / 2);
}

MegaphoneEditor::~MegaphoneEditor() = default;

void MegaphoneEditor::resized()
{
    panel->setBounds (0, 0, panel::kWidth, panel::kHeight);
    panel->setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) panel::kWidth));
}
