// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "game_object.h"

namespace mmo
{
	GameObject::GameObject(uint64 guid)
	{
		GameObject::PrepareFieldMap();

		// Setup fields
		m_fields.SetFieldValue<uint64>(object_fields::Guid, guid);
	}

	GameObject::~GameObject()
	{
	}
}
