// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "material_compiler.h"
#include "material.h"
#include "graphics/shader_compiler.h"
#include "log/default_log_levels.h"

namespace mmo
{
	void MaterialCompiler::Compile(Material& material, ShaderCompiler& shaderCompiler)
	{
		if (m_lit)
		{
			material.SetType(MaterialType::Opaque);
		}
		else
		{
			material.SetType(MaterialType::Unlit);
		}

		material.SetDepthWriteEnabled(m_depthWrite);
		material.SetDepthTestEnabled(m_depthTest);

		GenerateVertexShaderCode();
		GeneratePixelShaderCode();

		ShaderCompileInput vertexInput;
		vertexInput.shaderCode = m_vertexShaderCode;
		vertexInput.shaderType = ShaderType::VertexShader;
		ShaderCompileResult vertexOutput;
		shaderCompiler.Compile(vertexInput, vertexOutput);

		if (!vertexOutput.succeeded)
		{
			ELOG("Error compiling vertex shader: " << vertexOutput.errorMessage);
		}
		else
		{
			DLOG("Successfully compiled vertex shader. Size: " << vertexOutput.code.data.size());
		}
		
		ShaderCompileInput pixelInput;
		pixelInput.shaderCode = m_pixelShaderCode;
		pixelInput.shaderType = ShaderType::PixelShader;
		ShaderCompileResult pixelOutput;
		shaderCompiler.Compile(pixelInput, pixelOutput);

		if (!pixelOutput.succeeded)
		{
			ELOG("Error compiling pixel shader: " << pixelOutput.errorMessage);
		}
		else
		{
			DLOG("Successfully compiled pixel shader. Size: " << pixelOutput.code.data.size());
		}

		// Set material textures
		material.ClearTextures();
		for (const auto& texture : m_textures)
		{
			material.AddTexture(texture);
		}

		// Add shader code to the material
		material.SetVertexShaderCode({vertexOutput.code.data });
		material.SetPixelShaderCode({pixelOutput.code.data });
	}
	
	ExpressionType MaterialCompiler::GetExpressionType(ExpressionIndex index)
	{
		if (index < 0 || index >= m_expressionTypes.size())
		{
			return ExpressionType::Unknown;
		}

		return m_expressionTypes[index];
	}
}
