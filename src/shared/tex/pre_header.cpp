// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "pre_header.h"


namespace mmo
{
	namespace tex
	{
		PreHeader::PreHeader()
			: version(VersionId::Version_1_0)
		{
		}

		PreHeader::PreHeader(VersionId version)
			: version(version)
		{
		}
	}
}
