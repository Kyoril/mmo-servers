// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "frame.h"
#include "key.h"
#include "localization.h"
#include "localizer.h"

#include "base/non_copyable.h"
#include "base/utilities.h"

#include <string>
#include <functional>
#include <map>
#include <memory>


struct lua_State;


namespace mmo
{
	/// Handler for layout xml.
	class FrameManager final 
		: public NonCopyable
	{
	public:
		/// Type for a frame type factory.
		typedef std::function<FramePtr(const std::string& name)> FrameFactory;
		/// Type for a frame renderer factory.
		typedef std::function<std::unique_ptr<FrameRenderer>(const std::string& name)> RendererFactory;

	private:
		/// Contains a hash map of all registered frame factories.
		std::map<std::string, FrameFactory, StrCaseIComp> m_frameFactories;
		/// Contains a hash map of all registered frame renderer facotries.
		std::map<std::string, RendererFactory, StrCaseIComp> m_rendererFactories;

		std::map<std::string, std::vector<std::weak_ptr<Frame>>> m_eventFrames;

	private:
		friend std::unique_ptr<FrameManager> std::make_unique<FrameManager>();
		
		/// Private destructor to avoid instancing.
		FrameManager() = default;

	public:
		/// Singleton getter method.
		static FrameManager& Get();

	public:
		/// Initializes the frame manager by registering default frame factories.
		static void Initialize(lua_State* state);

		/// Deinitilaizes the frame manager, reverting everything done in Initialize().
		static void Destroy();

		Point GetUIScale() const { return m_uiScale; }

		Size GetUIScaleSize() const { return Size(m_uiScale.x, m_uiScale.y); }

	public:
		/// Loads files based on a given input stream with file contents.
		void LoadUIFile(const std::string& filename);

		/// Registers a new frame renderer factory by name.
		void RegisterFrameRenderer(const std::string& name, RendererFactory factory);

		/// Removes a registered frame renderer factory.
		void RemoveFrameRenderer(const std::string& name);

		/// Creates a frame renderer instance by name.
		std::unique_ptr<FrameRenderer> CreateRenderer(const std::string& name);
		
		void SetNativeResolution(const Size& nativeResolution);

		const Size& GetNativeResolution() const noexcept { return m_nativeResolution; }

	public:
		/// Contains info about a font map.
		struct FontMap final
		{
			std::string FontFile;
			float Size;
			float Outline;
		};

	public:
		/// Creates a new frame using the given type.
		FramePtr Create(const std::string& type, const std::string& name, bool isCopy = false);

		FramePtr CreateOrRetrieve(const std::string& type, const std::string& name);

		FramePtr Find(const std::string& name);

		void SetTopFrame(const FramePtr& topFrame);

		void ResetTopFrame();

		void Draw() const;

		void Update(float elapsedSeconds);
		
		/// Gets the currently hovered frame.
		inline FramePtr GetHoveredFrame() const { return m_hoverFrame; }

		/// Gets the frame that is currently capturing input events.
		inline FramePtr GetCaptureFrame() const { return m_inputCapture; }

		/// Notifies the FrameManager that the mouse cursor has been moved.
		void NotifyMouseMoved(const Point& position);

		/// Notifies the FrameManager that a mouse button was pressed.
		void NotifyMouseDown(MouseButton button, const Point& position);

		/// Notifies the FrameManager that a mouse button was released.
		void NotifyMouseUp(MouseButton button, const Point& position);

		/// Notifies the FrameManager that a key has been pressed.
		void NotifyKeyDown(Key key);

		/// Notifies the FrameManager that a key char has been generated by keyboard input.
		void NotifyKeyChar(uint16 codepoint);

		/// Notifies the FrameManager that a key has been released.
		void NotifyKeyUp(Key key);

		void NotifyScreenSizeChanged(float width, float height);

		/// Executes lua code.
		void ExecuteLua(const std::string& code);

		/// Triggers a lua event.
		template<typename ...Args>
		void TriggerLuaEvent(const std::string & eventName, Args&&... args)
		{
			auto eventIt = m_eventFrames.find(eventName);
			if (eventIt == m_eventFrames.end())
				return;

			// Iterate through every frame
			for (const auto& weakFrame : eventIt->second)
			{
				if (auto strongFrame = weakFrame.lock())
				{
					// Push this variable
					luabind::object o = luabind::object(m_luaState, strongFrame.get());

					// Raise event script
					strongFrame->TriggerEvent(eventName, o, args...);
				}
			}
		}

		/// Sets the frame that is currently capturing the input.
		void SetCaptureWindow(FramePtr capture);

		void FrameRegisterEvent(FramePtr frame, const std::string& eventName);

		void FrameUnregisterEvent(FramePtr frame, const std::string& eventName);

	public:
		/// Registers a new factory for a certain frame type.
		void RegisterFrameFactory(const std::string& elementName, FrameFactory factory);

		/// Removes a registered factory for a certain frame type.
		void UnregisterFrameFactory(const std::string& elementName);

		/// Removes all registered frame factories.
		void ClearFrameFactories();

		/// Adds a new font map.
		void AddFontMap(std::string name, FontMap map);

		/// Removes a font map by it's name.
		void RemoveFontMap(const std::string& name);

		/// Gets a font map by it's name.
		FontMap* GetFontMap(const std::string& name);

	public:
		/// Gets the root frame or nullptr if there is none.
		FramePtr GetTopFrame() const noexcept { return m_topFrame; }

		/// Gets the localization instance.
		const Localization& GetLocalization() const noexcept { return m_localization; }

	private:
		/// A map of mouse-down frames.
		std::map<MouseButton, FramePtr> m_mouseDownFrames;
		/// A map of all frames, keyed by their case insensitive name.
		std::map<std::string, FramePtr, StrCaseIComp> m_framesByName;
		/// The current root frame that is the one frame that is rendered.
		FramePtr m_topFrame;
		/// The currently hovered frame.
		FramePtr m_hoverFrame;
		/// Pressed mouse buttons.
		int32 m_pressedButtons;
		/// The lua state instance.
		lua_State* m_luaState;
		/// The frame that is currently capturing input events.
		FramePtr m_inputCapture;
		/// A map of font infos, keyed by a unique name. This is useful to make
		/// setting fonts in xml easier.
		std::map<std::string, FontMap, StrCaseIComp> m_fontMaps;
		/// The localization data.
		Localization m_localization;
		Size m_nativeResolution { 3840.0f, 2160.0f };
		Point m_uiScale { 1.0f, 1.0f };
	};
}