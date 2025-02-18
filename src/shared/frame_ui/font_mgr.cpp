// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "font_mgr.h"

#include "base/macros.h"
#include "log/default_log_levels.h"


namespace mmo
{
	FontManager & FontManager::Get()
	{
		static FontManager instance;
		return instance;
	}

	FontPtr FontManager::CreateOrRetrieve(const std::string & filename, float size, float outline, float shadowX, float shadowY)
	{
		auto font = FindCachedFont(filename, size, outline, shadowX, shadowY);
		if (font != nullptr)
		{
			return font;
		}

		font = std::make_shared<Font>();
		VERIFY(font->Initialize(filename, size, outline));

		font->SetShadow(shadowX, shadowY);

		m_fontCache[filename][size][outline] = font;

		return font;
	}

	FontPtr FontManager::FindCachedFont(const std::string & filename, float size, float outline, float shadowX, float shadowY) const
	{
		const auto cacheIt = m_fontCache.find(filename);
		if (cacheIt != m_fontCache.end())
		{
			const auto sizeIt = cacheIt->second.find(size);
			if (sizeIt != cacheIt->second.end())
			{
				const auto outlineIt = sizeIt->second.find(outline);
				if (outlineIt != sizeIt->second.end())
				{
					return outlineIt->second;
				}
			}
		}

		return nullptr;
	}
}
