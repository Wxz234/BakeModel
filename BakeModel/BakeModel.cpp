#define _CRT_SECURE_NO_WARNINGS

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/material.h>
#include <assimp/GltfMaterial.h>

#include <Windows.h>
#include <WinBase.h>
#include <fileapi.h>
#include <DirectXMath.h>

#include <iostream>
#include <vector>
#include <cstdint>
#include <string>
#include <optional>
#include <filesystem>
#include <fstream>
#include <limits>

#include "winrt/base.h"
#include "winrt/windows.foundation.h"
#include "winrt/windows.foundation.collections.h"
#include "winrt/windows.data.json.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

struct Vertex {
	float Position[3];
	float Normal[3];
	float TexCoords[2];
};

struct Texture {
	std::string FileName;
	std::optional<DirectX::XMFLOAT3> BaseColorFactor;
	std::optional<DirectX::XMFLOAT2> MetallicRoughnessFactor;
	std::optional<DirectX::XMFLOAT3> NormalFactor;
	std::optional<float> AOFactor;
};

struct Mesh {
	std::vector<Vertex> Vertices;
	std::vector<uint32_t> Indices;

	Texture BaseColor;
	Texture MetallicRoughness;
	Texture Normal;
	Texture AO;
};

void processMesh(std::vector<Mesh> &mMesh,aiMesh* mesh, const aiScene* scene)
{
	Mesh my_mesh;
	for (unsigned int i = 0; i < mesh->mNumVertices; i++)
	{
		Vertex vertex{};
		vertex.Position[0] = mesh->mVertices[i].x;
		vertex.Position[1] = mesh->mVertices[i].y;
		vertex.Position[2] = mesh->mVertices[i].z;
		vertex.Normal[0] = mesh->mNormals[i].x;
		vertex.Normal[1] = mesh->mNormals[i].y;
		vertex.Normal[2] = mesh->mNormals[i].z;

		if (!mesh->mTextureCoords[0]) {
			vertex.TexCoords[0] = 0.f;
			vertex.TexCoords[1] = 0.f;
		}
		else {
			vertex.TexCoords[0] = mesh->mTextureCoords[0][i].x;
			vertex.TexCoords[1] = mesh->mTextureCoords[0][i].y;
		}
		my_mesh.Vertices.push_back(vertex);
	}

	for (unsigned int i = 0; i < mesh->mNumFaces; i++)
	{
		aiFace face = mesh->mFaces[i];
		for (unsigned int j = 0; j < face.mNumIndices; j++) {
			my_mesh.Indices.push_back(face.mIndices[j]);
		}
	}
	
	aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];

	if (aiString baseColorTexture; material->GetTexture(AI_MATKEY_BASE_COLOR_TEXTURE, &baseColorTexture) == aiReturn_SUCCESS) {
		my_mesh.BaseColor.FileName = baseColorTexture.C_Str();
	}else if (aiColor3D color; material->Get(AI_MATKEY_BASE_COLOR, color) == aiReturn_SUCCESS) {
		my_mesh.BaseColor.BaseColorFactor = DirectX::XMFLOAT3(color.r, color.g, color.b);
	}
	else {
		my_mesh.BaseColor.BaseColorFactor = DirectX::XMFLOAT3(1.f, 1.f, 1.f);
	}

	if (aiString mrTexture; material->GetTexture(AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_METALLICROUGHNESS_TEXTURE, &mrTexture) == aiReturn_SUCCESS) {
		my_mesh.MetallicRoughness.FileName = mrTexture.C_Str();
	}
	else {
		DirectX::XMFLOAT2 mrFloat2(0.f, 0.f);
		if (float metallic; material->Get(AI_MATKEY_METALLIC_FACTOR, metallic) == aiReturn_SUCCESS) {
			mrFloat2.x = metallic;
		}
		else {
			mrFloat2.x = 1.f;
		}
		if (float roughness; material->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness) == aiReturn_SUCCESS) {
			mrFloat2.y = roughness;
		}
		else {
			mrFloat2.y = 1.f;
		}

		my_mesh.MetallicRoughness.MetallicRoughnessFactor = mrFloat2;
	}

	if (aiString normalTexture; material->GetTexture(aiTextureType_NORMALS, 0, &normalTexture) == aiReturn_SUCCESS) {
		my_mesh.Normal.FileName = normalTexture.C_Str();
	}
	else {
		my_mesh.Normal.NormalFactor = DirectX::XMFLOAT3(0.5f, 0.5f, 1.f);
	}

	if (aiString aoTexture; material->GetTexture(aiTextureType_AMBIENT_OCCLUSION, 0, &aoTexture) == aiReturn_SUCCESS) {
		my_mesh.AO.FileName = aoTexture.C_Str();
	}
	else {
		my_mesh.AO.AOFactor = 1.f;
	}

	mMesh.push_back(my_mesh);
}

void processNode(std::vector<Mesh>& mMesh,aiNode* node, const aiScene* scene)
{
	for (unsigned int i = 0; i < node->mNumMeshes; i++)
	{
		aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
		processMesh(mMesh, mesh, scene);
	}
	for (unsigned int i = 0; i < node->mNumChildren; i++)
	{
		processNode(mMesh, node->mChildren[i], scene);
	}
}

inline uint8_t float_to_int_color(const double color) {
	constexpr double MAXCOLOR = 256.0 - std::numeric_limits<double>::epsilon() * 128;
	return static_cast<uint8_t>(color * MAXCOLOR);
}

void Bake(std::filesystem::path& path, std::vector<Mesh>& mMesh) {
	auto file_name = path.stem();

	auto file_name_str = file_name.string();
	CreateDirectoryA(file_name_str.c_str(), nullptr);
	
	auto json_file= file_name_str + "\\" + file_name_str + ".json";
	std::ofstream json_file_out(json_file, std::fstream::out);
	auto json_bin = file_name_str + "\\" + file_name_str + ".bin";
	std::ofstream json_bin_out(json_bin, std::fstream::out);

	winrt::Windows::Data::Json::JsonObject json;
	json.Insert(L"MeshCount", winrt::Windows::Data::Json::JsonValue::CreateNumberValue(mMesh.size()));
	winrt::Windows::Data::Json::JsonArray mesh_attributes;

	int offset = 0;
	for (size_t i = 0;i < mMesh.size(); ++i) {
		winrt::Windows::Data::Json::JsonObject meshData;
		meshData.Insert(L"VertexCount", winrt::Windows::Data::Json::JsonValue::CreateNumberValue(mMesh[i].Vertices.size()));
		meshData.Insert(L"VertexOffset", winrt::Windows::Data::Json::JsonValue::CreateNumberValue(offset));

		auto vertex_data_size = mMesh[i].Vertices.size() * sizeof(float) * 8;
		offset += vertex_data_size;
		json_bin_out.write((const char*)mMesh[i].Vertices.data(), vertex_data_size);
		
		meshData.Insert(L"IndexCount", winrt::Windows::Data::Json::JsonValue::CreateNumberValue(mMesh[i].Indices.size()));
		meshData.Insert(L"IndexOffset", winrt::Windows::Data::Json::JsonValue::CreateNumberValue(offset));
		auto index_data_size = mMesh[i].Indices.size() * sizeof(uint32_t);
		offset += index_data_size;
		json_bin_out.write((const char*)mMesh[i].Indices.data(), index_data_size);

		if (mMesh[i].BaseColor.BaseColorFactor.has_value()) {

			auto texture_path = file_name_str + "\\Mesh" + std::to_string(i) + "BaseColor.png";
			uint8_t* picData = new uint8_t[16 * 16 * 3]{};
			for (int x = 0; x < 16; ++x) {
				for (int y = 0; y < 16; ++y) {
					auto factor_value = mMesh[i].BaseColor.BaseColorFactor.value();
					picData[x * 48 + y * 3 + 0] = float_to_int_color(factor_value.x);
					picData[x * 48 + y * 3 + 1] = float_to_int_color(factor_value.y);
					picData[x * 48 + y * 3 + 2] = float_to_int_color(factor_value.z);
				}
			}
			
			stbi_write_png(texture_path.c_str(), 16, 16, 3, picData, 0);
			auto json_tex_path = L"Mesh" + std::to_wstring(i) + L"BaseColor.png";
			
			meshData.Insert(L"BaseColorTexture", winrt::Windows::Data::Json::JsonValue::CreateStringValue(json_tex_path));
			delete []picData;
		}
		else {
			auto texture_path = path.parent_path();
			texture_path /= mMesh[i].BaseColor.FileName;
			std::filesystem::copy(texture_path, file_name, std::filesystem::copy_options::skip_existing);
			
			auto json_filename = texture_path.filename();
			auto json_tex_path = json_filename.wstring();
			meshData.Insert(L"BaseColorTexture", winrt::Windows::Data::Json::JsonValue::CreateStringValue(json_tex_path));
		}

		if (mMesh[i].MetallicRoughness.MetallicRoughnessFactor.has_value()) {
			auto texture_path = file_name_str + "\\Mesh" + std::to_string(i) + "MetallicRoughness.png";
			uint8_t* picData = new uint8_t[16 * 16 * 3]{};

			for (int x = 0; x < 16; ++x) {
				for (int y = 0; y < 16; ++y) {
					auto factor_value = mMesh[i].MetallicRoughness.MetallicRoughnessFactor.value();
					picData[x * 48 + y * 3 + 0] = float_to_int_color(factor_value.x);
					picData[x * 48 + y * 3 + 1] = float_to_int_color(factor_value.y);
					picData[x * 48 + y * 3 + 2] = 0;
				}
			}
			stbi_write_png(texture_path.c_str(), 16, 16, 3, picData, 0);
			auto json_tex_path = L"Mesh" + std::to_wstring(i) + L"MetallicRoughness.png";

			meshData.Insert(L"MetallicRoughnessTexture", winrt::Windows::Data::Json::JsonValue::CreateStringValue(json_tex_path));
			delete[]picData;
		}
		else {
			auto texture_path = path.parent_path();
			texture_path /= mMesh[i].MetallicRoughness.FileName;
			std::filesystem::copy(texture_path, file_name, std::filesystem::copy_options::skip_existing);

			auto json_filename = texture_path.filename();
			auto json_tex_path = json_filename.wstring();
			meshData.Insert(L"MetallicRoughnessTexture", winrt::Windows::Data::Json::JsonValue::CreateStringValue(json_tex_path));
		}

		if (mMesh[i].Normal.NormalFactor.has_value()) {
			auto texture_path = file_name_str + "\\Mesh" + std::to_string(i) + "Normal.png";
			uint8_t* picData = new uint8_t[16 * 16 * 3]{};

			for (int x = 0; x < 16; ++x) {
				for (int y = 0; y < 16; ++y) {
					auto factor_value = mMesh[i].Normal.NormalFactor.value();
					picData[x * 48 + y * 3 + 0] = float_to_int_color(factor_value.x);
					picData[x * 48 + y * 3 + 1] = float_to_int_color(factor_value.y);
					picData[x * 48 + y * 3 + 2] = float_to_int_color(factor_value.z);
				}
			}
			stbi_write_png(texture_path.c_str(), 16, 16, 3, picData, 0);
			auto json_tex_path = L"Mesh" + std::to_wstring(i) + L"Normal.png";

			meshData.Insert(L"NormalTexture", winrt::Windows::Data::Json::JsonValue::CreateStringValue(json_tex_path));
			delete[]picData;
		}
		else {
			auto texture_path = path.parent_path();
			texture_path /= mMesh[i].Normal.FileName;
			std::filesystem::copy(texture_path, file_name, std::filesystem::copy_options::skip_existing);
			auto json_filename = texture_path.filename();
			auto json_tex_path = json_filename.wstring();
			meshData.Insert(L"NormalTexture", winrt::Windows::Data::Json::JsonValue::CreateStringValue(json_tex_path));
		}

		if (mMesh[i].AO.AOFactor.has_value()) {
			auto texture_path = file_name_str + "\\Mesh" + std::to_string(i) + "AO.png";
			uint8_t* picData = new uint8_t[16 * 16 * 3]{};

			for (int x = 0; x < 16; ++x) {
				for (int y = 0; y < 16; ++y) {
					auto factor_value = mMesh[i].AO.AOFactor.value();
					picData[x * 48 + y * 3 + 0] = float_to_int_color(factor_value);
					picData[x * 48 + y * 3 + 1] = 255;
					picData[x * 48 + y * 3 + 2] = 255;
				}
			}
			stbi_write_png(texture_path.c_str(), 16, 16, 3, picData, 0);
			auto json_tex_path = L"Mesh" + std::to_wstring(i) + L"AO.png";

			meshData.Insert(L"AOTexture", winrt::Windows::Data::Json::JsonValue::CreateStringValue(json_tex_path));
			delete[]picData;
		}
		else {
			auto texture_path = path.parent_path();
			texture_path /= mMesh[i].AO.FileName;
			std::filesystem::copy(texture_path, file_name, std::filesystem::copy_options::skip_existing);

			auto json_filename = texture_path.filename();
			auto json_tex_path = json_filename.wstring();
			meshData.Insert(L"AOTexture", winrt::Windows::Data::Json::JsonValue::CreateStringValue(json_tex_path));
		}

		mesh_attributes.InsertAt(i, meshData);
	}
	
	json.Insert(L"MeshAttributes", mesh_attributes);

	auto jsonStr = json.Stringify();
	auto json_s_Str = winrt::to_string(jsonStr);
	json_file_out.write(json_s_Str.data(), json_s_Str.size());
}


int main(int argc, char* argv[])
{
	winrt::init_apartment();

	Assimp::Importer importer;
	const aiScene* scene = nullptr;
	if (argc >= 2) {
		scene = importer.ReadFile(argv[1], aiProcess_Triangulate | aiProcess_GenNormals);
	}
	else {
		std::cout << "Need file name." << std::endl;
		return 0;
	}
	std::vector<Mesh> mMesh;
	scene = importer.ReadFile(argv[1], aiProcess_Triangulate | aiProcess_GenNormals);
	std::filesystem::path my_path{ argv[1] };
	if (scene) {
		processNode(mMesh, scene->mRootNode, scene);
	}

	Bake(my_path, mMesh);

	return 0;
}
