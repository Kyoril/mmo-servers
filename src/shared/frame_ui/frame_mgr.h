// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#pragma once

#include "frame.h"

#include "base/non_copyable.h"
#include "base/utilities.h"

#include <string>
#include <functional>
#include <map>


namespace mmo
{
	/// Handler for layout xml.
	class FrameManager final 
		: public NonCopyable
	{
	public:
		/// Type for a frame type factor.
		typedef std::function<FramePtr(const std::string& name)> FrameFactory;

	private:
		/// Contains a hash map of all registered frame factories.
		std::map<std::string, FrameFactory, StrCaseIComp> m_frameFactories;

	private:
		/// Private destructor to avoid instancing.
		FrameManager() = default;

	public:
		/// Singleton getter method.
		static FrameManager& Get();

	public:
		/// Initializes the frame manager by registering default frame factories.
		static void Initialize();
		/// Deinitilaizes the frame manager, reverting everything done in Initialize().
		static void Destroy();

	public:
		/// Loads files based on a given input stream with file contents.
		void LoadUIFile(const std::string& filename);

	public:
		/// Creates a new frame using the given type.
		FramePtr Create(const std::string& type, const std::string& name, bool isCopy = false);
		FramePtr CreateOrRetrieve(const std::string& type, const std::string& name);
		FramePtr Find(const std::string& name);
		void SetTopFrame(const FramePtr& topFrame);
		void ResetTopFrame();
		void Draw() const;
		/// Gets the currently hovered frame.
		inline FramePtr GetHoveredFrame() const { return m_hoverFrame; }
		/// Notifies the FrameManager that the mouse cursor has been moved.
		void NotifyMouseMoved(const Point& position);

	public:
		/// Registers a new factory for a certain frame type.
		void RegisterFrameFactory(const std::string& elementName, FrameFactory factory);
		/// Removes a registered factory for a certain frame type.
		void UnregisterFrameFactory(const std::string& elementName);
		/// Removes all registered frame factories.
		void ClearFrameFactories();

	public:
		/// Gets the root frame or nullptr if there is none.
		inline FramePtr GetTopFrame() const { return m_topFrame; }

	private:
		/// A map of all frames, keyed by their case insensitive name.
		std::map<std::string, FramePtr, StrCaseIComp> m_framesByName;
		/// The current root frame that is the one frame that is rendered.
		FramePtr m_topFrame;
		/// The currently hovered frame.
		FramePtr m_hoverFrame;
	};
}