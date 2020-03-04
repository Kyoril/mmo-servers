// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "text_component.h"
#include "geometry_buffer.h"
#include "frame.h"

#include "base/utilities.h"


namespace mmo
{
	TextComponent::TextComponent(const std::string & fontFile, float fontSize, float outline)
		: FrameComponent()
	{
		m_font = std::make_shared<Font>();
		VERIFY(m_font->Initialize(fontFile, fontSize, outline));
	}

	void TextComponent::SetHorizontalAlignment(HorizontalAlignment alignment)
	{
		m_horzAlignment = alignment;
	}

	void TextComponent::SetVerticalAlignment(VerticalAlignment alignment)
	{
		m_vertAlignment = alignment;
	}

	void TextComponent::Render(Frame& frame) const
	{
		if (m_font)
		{
			// Calculate the frame rectangle
			const Rect frameRect = frame.GetAbsoluteFrameRect();

			// Calculate the text width and cache it for later use
			const float width = m_font->GetTextWidth(frame.GetText());

			// Calculate final text position in component
			Point position = frameRect.GetPosition();

			// Apply horizontal alignment
			if (m_horzAlignment == HorizontalAlignment::Center)
			{
				position.x += frameRect.GetWidth() * 0.5f - width * 0.5f;
			}
			else if (m_horzAlignment == HorizontalAlignment::Right)
			{
				position.x += frameRect.GetWidth() - width;
			}

			// TODO: apply vertical alignment formatting

			// Determine the position to render the font at
			m_font->DrawText(frame.GetText(), position, frame.GetGeometryBuffer());
		}
	}

	VerticalAlignment VerticalAlignmentByName(const std::string & name)
	{
		if (_stricmp(name.c_str(), "CENTER") == 0)
		{
			return VerticalAlignment::Center;
		}
		else if (_stricmp(name.c_str(), "BOTTOM") == 0)
		{
			return VerticalAlignment::Bottom;
		}

		// Default value
		return VerticalAlignment::Top;
	}

	std::string VerticalAlignmentName(VerticalAlignment alignment)
	{
		switch (alignment)
		{
		case VerticalAlignment::Top:
			return "TOP";
		case VerticalAlignment::Center:
			return "CENTER";
		case VerticalAlignment::Bottom:
			return "BOTTOM";
		default:
			// Default value
			return "TOP";
		}
	}

	HorizontalAlignment HorizontalAlignmentByName(const std::string & name)
	{
		if (_stricmp(name.c_str(), "CENTER") == 0)
		{
			return HorizontalAlignment::Center;
		}
		else if (_stricmp(name.c_str(), "RIGHT") == 0)
		{
			return HorizontalAlignment::Right;
		}

		// Default value
		return HorizontalAlignment::Left;
	}

	std::string HorizontalAlignmentName(HorizontalAlignment alignment)
	{
		switch (alignment)
		{
		case HorizontalAlignment::Left:
			return "LEFT";
		case HorizontalAlignment::Center:
			return "CENTER";
		case HorizontalAlignment::Right:
			return "RIGHT";
		default:
			// Default value
			return "LEFT";
		}
	}
}
