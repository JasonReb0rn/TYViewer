template<>
inline Texture* Content::load(const std::string& name)
{
	if (archive == NULL)
	{
		Debug::log("Failed to load asset because no archive is loaded!");
		return defaultTexture;
	}

	if (textures.find(name) != textures.end())
	{
		return textures[name];
	}
	else
	{
		std::vector<char> data;
		if (archive->getFileData(name, data))
		{
			unsigned int id = SOIL_load_OGL_texture_from_memory(reinterpret_cast<unsigned char*>(&data[0]), static_cast<int>(data.size()), 0, 0, SOIL_FLAG_INVERT_Y);
			textures[name] = new Texture(id);

			return textures[name];
		}
	}

	return defaultTexture;
}
template<>
inline Shader* Content::load(const std::string& name)
{
	if (archive == NULL)
	{
		Debug::log("Failed to load asset because no archive is loaded!");
		return NULL;
	}

	if (shaders.find(name) != shaders.end())
	{
		return shaders[name];
	}
	else
	{
		File file;
		bool foundInArchive = archive->getFile(name, file);
		
		std::ifstream stream;
		bool useArchive = false;

		if (foundInArchive)
		{
			Debug::log("Loading shader from archive: " + name);
			stream.open(archive->path, std::ios::binary);
			if (stream.is_open())
			{
				stream.seekg(file.offset, std::ios::beg);
				if (!stream.fail())
				{
					useArchive = true;
				}
				else
				{
					stream.close();
				}
			}
		}

		// Fallback: Create default shader if file not found
		if (!useArchive)
		{
			Debug::log("Shader file not found, creating default shader: " + name);
			Shader* defaultShader = Shader::createDefault();
			if (defaultShader == nullptr)
			{
				Debug::log("ERROR: Failed to create default shader");
				return NULL;
			}
			shaders[name] = defaultShader;
			Debug::log("Successfully created default shader: " + name);
			return shaders[name];
		}

		try
		{
			std::unordered_map<std::string, int> properties;
			properties["TEX"] = 1;
			properties["AREF"] = 0;
			properties["LIT"] = 0;
			properties["SHADOW"] = 0;
			properties["FOG"] = 0;
			properties["TEXMTX"] = 0;
			properties["BLACKTRANS"] = 0;
			properties["SKIN"] = 0;
			properties["SHADOWNORMS"] = 0;
			properties["OMNI"] = 0;

			Shader* shader = new Shader(stream, properties);
			
			if (shader == nullptr)
			{
				Debug::log("ERROR: Shader creation returned null");
				stream.close();
				return NULL;
			}

			shaders[name] = shader;
			stream.close();

			Debug::log("Successfully loaded shader: " + name);
			return shaders[name];
		}
		catch (const std::exception& e)
		{
			Debug::log("ERROR: Exception while loading shader " + name + ": " + e.what());
			stream.close();
			return NULL;
		}
		catch (...)
		{
			Debug::log("ERROR: Unknown exception while loading shader: " + name);
			stream.close();
			return NULL;
		}
	}

	return NULL;
}

template<>
inline Model* Content::load(const std::string& name)
{
	if (archive == NULL)
	{
		Debug::log("Failed to load asset because no archive is loaded!");
		return NULL;
	}

	if (models.find(name) != models.end())
	{
		return models[name];
	}
		else
		{
			std::vector<char> data;
			if (!archive->getFileData(name, data))
			{
				Debug::log("Model file not found in archive: " + name);
				return NULL;
			}

			// Check if this is TY 2 format (has corresponding .mdg file) BEFORE parsing MDL
			std::string baseName = name;
			size_t dotPos = baseName.find_last_of('.');
			if (dotPos != std::string::npos)
			{
				baseName = baseName.substr(0, dotPos);
			}
			std::string mdgName = baseName + ".mdg";

			std::vector<char> mdgData;
			bool isTY2 = archive->getFileData(mdgName, mdgData);

			mdl2 mdl;
			bool loaded = false;
			
			if (isTY2)
			{
				// Try TY 2 format first (relaxed signature check)
				Debug::log("Attempting to load as TY 2 format...");
				Debug::log("MDL file size: " + std::to_string(data.size()) + " bytes");
				try
				{
					loaded = mdl.loadTY2(data.data(), 0);
					if (loaded)
					{
						Debug::log("Successfully loaded TY 2 MDL file");
					}
					else
					{
						Debug::log("TY 2 format failed, trying TY 1 format...");
						loaded = mdl.load(data.data(), 0);
					}
				}
				catch (const std::exception& e)
				{
					Debug::log("ERROR: Exception in loadTY2: " + std::string(e.what()));
					loaded = false;
				}
				catch (...)
				{
					Debug::log("ERROR: Unknown exception in loadTY2");
					loaded = false;
				}
			}
			else
			{
				// Try TY 1 format
				loaded = mdl.load(data.data(), 0);
			}

			if (!loaded)
			{
				uint32_t signature = from_bytes<uint32_t>(data.data(), 0);
				Debug::log("Failed to parse MDL file (invalid format or signature): " + name);
				Debug::log("MDL signature: " + std::to_string(signature) + " (expected TY 1: " + std::to_string(843859021) + ")");
				return NULL;
			}

			if (mdl.subobjects.empty())
			{
				Debug::log("Warning: MDL file has no subobjects: " + name);
			}

			std::vector<Mesh*> meshes;

			std::vector<Collider> colliders;
			std::vector<Bounds> bounds;
			std::vector<Bone> bones;

			if (isTY2)
			{
				Debug::log("Detected TY 2 format, loading MDG file: " + mdgName);
				// TY 2 format: Load mesh data from .mdg file
				mdg mdgParser;
				bool mdgLoaded = false;
				
				// Try loading with MDL3 metadata if available
				if (mdl.isMDL3Format)
				{
					Debug::log("Using MDL3 metadata to parse MDG file");
					mdgLoaded = mdgParser.loadWithMDL3Metadata(mdgData.data(), mdgData.size(), mdl.mdl3Metadata, data.data(), 0);
				}
				else
				{
					// Fallback to generic MDG parsing
					mdgLoaded = mdgParser.load(mdgData.data(), mdgData.size());
				}
				
				if (mdgLoaded)
				{
					Debug::log("Successfully loaded MDG file with " + std::to_string(mdgParser.meshes.size()) + " meshes");
					auto deriveStripRanges = [](const std::vector<Vertex>& vertices)
					{
						std::vector<std::pair<size_t, size_t>> ranges;
						const auto isSamePosition = [](const Vertex& a, const Vertex& b)
						{
							return std::abs(a.position.x - b.position.x) < 0.00001f &&
								std::abs(a.position.y - b.position.y) < 0.00001f &&
								std::abs(a.position.z - b.position.z) < 0.00001f;
						};

						const size_t count = vertices.size();
						size_t startIndex = 0;
						size_t i = 0;
						while (i + 1 < count)
						{
							if (isSamePosition(vertices[i], vertices[i + 1]))
							{
								if (i + 1 > startIndex)
								{
									ranges.push_back({ startIndex, (i - startIndex) + 1 });
								}

								// If we have two duplicate pairs back-to-back, treat them as one connector.
								if (i + 3 < count && isSamePosition(vertices[i + 2], vertices[i + 3]))
								{
									startIndex = i + 2;
									if (startIndex + 1 < count && isSamePosition(vertices[startIndex], vertices[startIndex + 1]))
									{
										startIndex += 1;
									}
									i = startIndex;
									continue;
								}

								startIndex = i + 1;
								i = startIndex;
								continue;
							}
							i++;
						}

						if (startIndex < count)
						{
							ranges.push_back({ startIndex, count - startIndex });
						}

						return ranges;
					};

					auto appendTriangleStripIndices = [](const std::vector<Vertex>& vertices, std::vector<unsigned int>& indices, size_t startIndex, size_t count, size_t& degenerateCount, size_t& totalTriangleCount, size_t& degenerateUvMismatch, size_t& stripBreaks)
					{
						const auto isSamePosition = [](const Vertex& a, const Vertex& b)
						{
							return std::abs(a.position.x - b.position.x) < 0.00001f &&
								std::abs(a.position.y - b.position.y) < 0.00001f &&
								std::abs(a.position.z - b.position.z) < 0.00001f;
						};
						const auto isSameUv = [](const Vertex& a, const Vertex& b)
						{
							return std::abs(a.texcoord.x - b.texcoord.x) < 0.00001f &&
								std::abs(a.texcoord.y - b.texcoord.y) < 0.00001f;
						};

						if (count < 3 || startIndex + count > vertices.size())
						{
							return;
						}

						totalTriangleCount += (count - 2);
						for (size_t i = 0; i + 2 < count; i++)
						{
							unsigned int i0 = static_cast<unsigned int>(startIndex + i);
							unsigned int i1 = static_cast<unsigned int>(startIndex + i + 1);
							unsigned int i2 = static_cast<unsigned int>(startIndex + i + 2);

							// Skip degenerate triangles (strip connectors)
							bool deg01 = isSamePosition(vertices[i0], vertices[i1]);
							bool deg12 = isSamePosition(vertices[i1], vertices[i2]);
							bool deg02 = isSamePosition(vertices[i0], vertices[i2]);
							if (deg01 || deg12 || deg02)
							{
								degenerateCount++;
								if ((deg01 && !isSameUv(vertices[i0], vertices[i1])) ||
									(deg12 && !isSameUv(vertices[i1], vertices[i2])) ||
									(deg02 && !isSameUv(vertices[i0], vertices[i2])))
								{
									degenerateUvMismatch++;
									stripBreaks++;
								}
							}

							if ((i & 1) == 0)
							{
								indices.push_back(i0);
								indices.push_back(i1);
								indices.push_back(i2);
							}
							else
							{
								indices.push_back(i1);
								indices.push_back(i0);
								indices.push_back(i2);
							}
						}
					};

					// Use MDG meshes directly - they already have texture/component info if loaded with MDL3 metadata
					if (mdl.isMDL3Format)
					{
						// MDG meshes are already organized by texture/component
						Debug::log("Using MDG meshes with MDL3 metadata");
						for (size_t i = 0; i < mdgParser.meshes.size(); i++)
						{
							std::vector<Vertex> vertices;
							std::vector<unsigned int> indices;

							// Get vertices from MDG
							auto& mdgMesh = mdgParser.meshes[i];

							for (auto& vertex : mdgMesh.vertices)
							{
								Vertex v =
								{
									glm::vec4(vertex.position[0], vertex.position[1], vertex.position[2], 1.0f),
									glm::vec4(vertex.normal[0], vertex.normal[1], vertex.normal[2], 1.0f),
									glm::vec4(vertex.colour[0], vertex.colour[1], vertex.colour[2], vertex.colour[3]),
									glm::vec2(vertex.texcoord[0], vertex.texcoord[1]),
									glm::vec3(vertex.skin[0], vertex.skin[1], vertex.skin[2])
								};

								vertices.push_back(v);
							}

							// Generate triangle strip indices with degenerate handling
							size_t degenerateCount = 0;
							size_t totalTriangleCount = 0;
							size_t degenerateUvMismatch = 0;
							size_t stripBreaks = 0;
							bool usedDerivedRanges = false;
							if (!mdgMesh.stripVertexCounts.empty())
							{
								size_t startIndex = 0;
								size_t stripSum = 0;
								for (auto stripVertexCount : mdgMesh.stripVertexCounts)
								{
									stripSum += stripVertexCount;
								}
								const size_t stripCount = mdgMesh.stripVertexCounts.size();
								const size_t stripDegenerate2Sum = stripSum + (stripCount > 0 ? (stripCount - 1) * 2 : 0);
								const size_t stripDegenerate1Sum = stripSum + (stripCount > 0 ? (stripCount - 1) : 0);
								const bool countsIncludeDegenerates = (stripSum == vertices.size());
								const bool countsExcludeDegenerates2 = (stripDegenerate2Sum == vertices.size());
								const bool countsExcludeDegenerates1 = (!countsExcludeDegenerates2 && stripDegenerate1Sum == vertices.size());
								const bool countsAlign = countsIncludeDegenerates || countsExcludeDegenerates2 || countsExcludeDegenerates1;

								if (!countsAlign)
								{
									auto ranges = deriveStripRanges(vertices);
									if (!ranges.empty())
									{
										for (const auto& range : ranges)
										{
											if (range.second >= 3)
											{
												appendTriangleStripIndices(vertices, indices, range.first, range.second, degenerateCount, totalTriangleCount, degenerateUvMismatch, stripBreaks);
											}
										}
										usedDerivedRanges = true;
									}
								}

								if (usedDerivedRanges)
								{
									Debug::log("MDG PC: Derived strips from degenerate connectors");
								}
								else
								{
								for (auto stripVertexCount : mdgMesh.stripVertexCounts)
								{
									if (stripVertexCount >= 3)
									{
										appendTriangleStripIndices(vertices, indices, startIndex, stripVertexCount, degenerateCount, totalTriangleCount, degenerateUvMismatch, stripBreaks);
									}
									if (countsIncludeDegenerates)
									{
										startIndex += stripVertexCount;
									}
									else if (countsExcludeDegenerates2)
									{
										startIndex += stripVertexCount + 2;
									}
									else if (countsExcludeDegenerates1)
									{
										startIndex += stripVertexCount + 1;
									}
									else
									{
										startIndex += stripVertexCount;
									}
								}
								}
							}
							else
							{
								auto ranges = deriveStripRanges(vertices);
								if (!ranges.empty())
								{
									for (const auto& range : ranges)
									{
										if (range.second >= 3)
										{
											appendTriangleStripIndices(vertices, indices, range.first, range.second, degenerateCount, totalTriangleCount, degenerateUvMismatch, stripBreaks);
										}
									}
									Debug::log("MDG PC: Derived strips from degenerate connectors");
								}
								else
								{
									appendTriangleStripIndices(vertices, indices, 0, vertices.size(), degenerateCount, totalTriangleCount, degenerateUvMismatch, stripBreaks);
								}
							}
							Debug::log("MDG PC: Degenerate triangles skipped: " + std::to_string(degenerateCount) +
								" of " + std::to_string(totalTriangleCount) +
								" (uv mismatch: " + std::to_string(degenerateUvMismatch) +
								", strip breaks: " + std::to_string(stripBreaks) + ")");

							// Use texture name from MDL3 metadata
							std::string textureName = "";
							if (mdgMesh.textureIndex < mdl.mdl3Metadata.TextureNames.size())
							{
								textureName = mdl.mdl3Metadata.TextureNames[mdgMesh.textureIndex];
							}
							
							Texture* texture = load<Texture>(textureName + ".dds");
							if (texture == defaultTexture && !textureName.empty())
							{
								std::cout << "Failed to load texture: '" + textureName + "' !" << std::endl
									<< "-!- This should not appear after fully implementing materials! -!-" << std::endl;
							}
							meshes.push_back(new Mesh(vertices, indices, texture));
						}
					}
					else
					{
						// Fallback: Use MDG meshes directly without MDL3 metadata
						Debug::log("Using MDG meshes without MDL3 metadata (fallback)");
						for (size_t i = 0; i < mdgParser.meshes.size(); i++)
						{
							std::vector<Vertex> vertices;
							std::vector<unsigned int> indices;

							// Get vertices from MDG
							auto& mdgMesh = mdgParser.meshes[i];

							for (auto& vertex : mdgMesh.vertices)
							{
								Vertex v =
								{
									glm::vec4(vertex.position[0], vertex.position[1], vertex.position[2], 1.0f),
									glm::vec4(vertex.normal[0], vertex.normal[1], vertex.normal[2], 1.0f),
									glm::vec4(vertex.colour[0], vertex.colour[1], vertex.colour[2], vertex.colour[3]),
									glm::vec2(vertex.texcoord[0], vertex.texcoord[1]),
									glm::vec3(vertex.skin[0], vertex.skin[1], vertex.skin[2])
								};

								vertices.push_back(v);
							}

							// Generate triangle strip indices with degenerate handling
							size_t degenerateCount = 0;
							size_t totalTriangleCount = 0;
							size_t degenerateUvMismatch = 0;
							size_t stripBreaks = 0;
							bool usedDerivedRanges = false;
							if (!mdgMesh.stripVertexCounts.empty())
							{
								size_t startIndex = 0;
								size_t stripSum = 0;
								for (auto stripVertexCount : mdgMesh.stripVertexCounts)
								{
									stripSum += stripVertexCount;
								}
								const size_t stripCount = mdgMesh.stripVertexCounts.size();
								const size_t stripDegenerate2Sum = stripSum + (stripCount > 0 ? (stripCount - 1) * 2 : 0);
								const size_t stripDegenerate1Sum = stripSum + (stripCount > 0 ? (stripCount - 1) : 0);
								const bool countsIncludeDegenerates = (stripSum == vertices.size());
								const bool countsExcludeDegenerates2 = (stripDegenerate2Sum == vertices.size());
								const bool countsExcludeDegenerates1 = (!countsExcludeDegenerates2 && stripDegenerate1Sum == vertices.size());
								const bool countsAlign = countsIncludeDegenerates || countsExcludeDegenerates2 || countsExcludeDegenerates1;

								if (!countsAlign)
								{
									auto ranges = deriveStripRanges(vertices);
									if (!ranges.empty())
									{
										for (const auto& range : ranges)
										{
											if (range.second >= 3)
											{
												appendTriangleStripIndices(vertices, indices, range.first, range.second, degenerateCount, totalTriangleCount, degenerateUvMismatch, stripBreaks);
											}
										}
										usedDerivedRanges = true;
									}
								}

								if (usedDerivedRanges)
								{
									Debug::log("MDG PC: Derived strips from degenerate connectors");
								}
								else
								{
								for (auto stripVertexCount : mdgMesh.stripVertexCounts)
								{
									if (stripVertexCount >= 3)
									{
										appendTriangleStripIndices(vertices, indices, startIndex, stripVertexCount, degenerateCount, totalTriangleCount, degenerateUvMismatch, stripBreaks);
									}
									if (countsIncludeDegenerates)
									{
										startIndex += stripVertexCount;
									}
									else if (countsExcludeDegenerates2)
									{
										startIndex += stripVertexCount + 2;
									}
									else if (countsExcludeDegenerates1)
									{
										startIndex += stripVertexCount + 1;
									}
									else
									{
										startIndex += stripVertexCount;
									}
								}
								}
							}
							else
							{
								auto ranges = deriveStripRanges(vertices);
								if (!ranges.empty())
								{
									for (const auto& range : ranges)
									{
										if (range.second >= 3)
										{
											appendTriangleStripIndices(vertices, indices, range.first, range.second, degenerateCount, totalTriangleCount, degenerateUvMismatch, stripBreaks);
										}
									}
									Debug::log("MDG PC: Derived strips from degenerate connectors");
								}
								else
								{
									appendTriangleStripIndices(vertices, indices, 0, vertices.size(), degenerateCount, totalTriangleCount, degenerateUvMismatch, stripBreaks);
								}
							}
							Debug::log("MDG PC: Degenerate triangles skipped: " + std::to_string(degenerateCount) +
								" of " + std::to_string(totalTriangleCount) +
								" (uv mismatch: " + std::to_string(degenerateUvMismatch) +
								", strip breaks: " + std::to_string(stripBreaks) + ")");

							// Use default texture since we don't have material info
							meshes.push_back(new Mesh(vertices, indices, defaultTexture));
						}
					}
				}
				else
				{
					Debug::log("Failed to parse MDG file (no valid mesh data found): " + mdgName);
					return NULL;
				}
			}
			else
			{
				Debug::log("Detected TY 1 format (no MDG file found), using embedded vertex data");
				// TY 1 format: Use embedded vertex data from .mdl file
				for (auto& subobj : mdl.subobjects)
				{
					for (auto& mesh : subobj.meshes)
					{
						std::vector<Vertex> vertices;
						std::vector<unsigned int> indices;

						int triangleIndex = 0;
						for (auto& segment : mesh.segments)
						{
							for (auto& vertex : segment.vertices)
							{
								Vertex v =
								{
									glm::vec4(vertex.position[0], vertex.position[1],vertex.position[2], 1.0f),
									glm::vec4(vertex.normal[0], vertex.normal[1],vertex.normal[2], 1.0f),
									glm::vec4(vertex.colour[0], vertex.colour[1],vertex.colour[2], vertex.colour[3]),
									glm::vec2(vertex.texcoord[0], vertex.texcoord[1]),
									glm::vec3(vertex.skin[0], vertex.skin[1], vertex.skin[2])
								};

								vertices.push_back(v);
							}

							for (unsigned int i = 0; i < segment.vertices.size() - 2; i++)
							{
								indices.push_back(0 + triangleIndex);
								indices.push_back(2 + triangleIndex);
								indices.push_back(1 + triangleIndex);

								triangleIndex++;
							}
							triangleIndex += 2;
						}

						Texture* texture = load<Texture>(mesh.material + ".dds");
						if (texture == defaultTexture)
						{
							std::cout << "Failed to load texture: '" + mesh.material + "' !" << std::endl
								<< "-!- This should not appear after fully implementing materials! -!-" << std::endl;
						}
						meshes.push_back(new Mesh(vertices, indices, texture));
					}
				}
			}
			
			
			/*
			unsigned int amount_of_subobjects = from_bytes<uint16_t>(&data[0], 6);
			unsigned int amount_of_colliders = from_bytes<uint16_t>(&data[0], 8);
			unsigned int amount_of_bones = from_bytes<uint16_t>(&data[0], 10);

			int offset_subobjects = from_bytes<uint32_t>(&data[0], 12);
			int offset_colliders = from_bytes<uint32_t>(&data[0], 16);
			int offset_bones = from_bytes<uint32_t>(&data[0], 20);

			glm::vec3 bounds_crn = glm::vec3(
				from_bytes<float>(&data[0], 32),
				from_bytes<float>(&data[0], 36),
				from_bytes<float>(&data[0], 40));

			glm::vec3 bounds_size = glm::vec3(
				from_bytes<float>(&data[0], 48),
				from_bytes<float>(&data[0], 52),
				from_bytes<float>(&data[0], 56));

			for (int offset = offset_subobjects; offset < offset_subobjects + amount_of_subobjects * MDL2_SUBMESH_SIZE; offset += MDL2_SUBMESH_SIZE)
			{
				glm::vec3 s_bounds_crn = glm::vec3(
					from_bytes<float>(&data[0], offset),
					from_bytes<float>(&data[0], offset + 4),
					from_bytes<float>(&data[0], offset + 8));

				glm::vec3 s_bounds_max = glm::vec3(
					from_bytes<float>(&data[0], offset + 16),
					from_bytes<float>(&data[0], offset + 20),
					from_bytes<float>(&data[0], offset + 24));

				bounds.push_back({ s_bounds_crn, s_bounds_max });


				int submesh_count	= from_bytes<int16_t>(&data[0], offset + 66);
				int submesh_offset	= from_bytes<int16_t>(&data[0], offset + 68);

				for (unsigned int i = 0; i < submesh_count; i++)
				{
					std::vector<Vertex> vertices;
					std::vector<unsigned int> indices;

					std::string submesh_ident = "";
					submesh_ident = nts(&data[0], from_bytes<int32_t>(&data[0], submesh_offset + (i * 16)));

					std::string segment_material = "";
					segment_material = nts(&data[0], from_bytes<int32_t>(&data[0], submesh_offset + (i * 16) + 0));

					int segment_offset = from_bytes<int32_t>(&data[0], submesh_offset + (i * 16) + 4);
					int segment_count = from_bytes<int32_t>(&data[0], submesh_offset + (i * 16) + 12);


					int triangle_index = 0;

					for (int j = 0; j < segment_count; j++)
					{
						int vertex_count = from_bytes<int32_t>(&data[0], segment_offset + MDL2_SEGMENT_VERTEX_LIST_COUNT_OFFSET);

						for (int k = 0; k < vertex_count; k++)
						{
							Vertex vertex =
							{
								// position
								glm::vec4
								(
									from_bytes<float>(&data[0], segment_offset + MDL2_SEGMENT_VERTEX_LIST_OFFSET + (k * 12) + 0),
									from_bytes<float>(&data[0], segment_offset + MDL2_SEGMENT_VERTEX_LIST_OFFSET + (k * 12) + 4),
									from_bytes<float>(&data[0], segment_offset + MDL2_SEGMENT_VERTEX_LIST_OFFSET + (k * 12) + 8),
									1.0f
								),

								// normal
								glm::vec4
								(
									byte_to_single(&data[0], segment_offset + MDL2_SEGMENT_VERTEX_LIST_OFFSET + vertex_count * 12 + 4 + (k * 4) + 0),
									byte_to_single(&data[0], segment_offset + MDL2_SEGMENT_VERTEX_LIST_OFFSET + vertex_count * 12 + 4 + (k * 4) + 1),
									byte_to_single(&data[0], segment_offset + MDL2_SEGMENT_VERTEX_LIST_OFFSET + vertex_count * 12 + 4 + (k * 4) + 2),
									1.0f
								),

								// colour
								glm::vec4
								(
									1.0f,
									1.0f,
									1.0f,
									1.0f
								),

								// texcoord
								glm::vec2
								(
									from_bytes<int16_t>(&data[0], segment_offset + MDL2_SEGMENT_VERTEX_LIST_OFFSET + vertex_count * 12 + 4 + vertex_count * 4 + 4 + k * 8) / 4096.0f,
									abs((from_bytes<int16_t>(&data[0], segment_offset + MDL2_SEGMENT_VERTEX_LIST_OFFSET + vertex_count * 12 + 4 + vertex_count * 4 + 4 + k * 8 + 2) / 4096.0f) - 1.0f)
								),

								// skin
								glm::vec3
								(
									from_bytes<int16_t>(&data[0], segment_offset + MDL2_SEGMENT_VERTEX_LIST_OFFSET + vertex_count * 12 + 4 + vertex_count * 4 + 4 + k * 8 + 4) / 4096.0f,
									from_bytes<int8_t>(&data[0], segment_offset + MDL2_SEGMENT_VERTEX_LIST_OFFSET + vertex_count * 12 + 4 + vertex_count * 4 + 4 + k * 8 + 6),
									from_bytes<int8_t>(&data[0], segment_offset + MDL2_SEGMENT_VERTEX_LIST_OFFSET + vertex_count * 12 + 4 + vertex_count * 4 + 4 + k * 8 + 7)
								)
							};

							vertices.push_back(vertex);
						}

						for (int k = 0; k < vertex_count - 2; k++)
						{
							indices.push_back(0 + triangle_index);
							indices.push_back(2 + triangle_index);
							indices.push_back(1 + triangle_index);

							triangle_index++;
						}
						triangle_index += 2;

						segment_offset += MDL2_SEGMENT_VERTEX_LIST_OFFSET + vertex_count * 12 + 4 + vertex_count * 4 + 4 + vertex_count * 8;

						while (from_bytes<int16_t>(&data[0], segment_offset) != -1)
						{
							segment_offset += 2;
						}
					}

					meshes.push_back(new Mesh(vertices, indices, load<Texture>(submesh_ident + ".dds")));
				}
			}

			for (int offset = offset_colliders; offset < offset_colliders + amount_of_colliders * 32; offset += 32)
			{
				glm::vec3 position(
					from_bytes<float>(&data[0], offset),
					from_bytes<float>(&data[0], offset + 4),
					from_bytes<float>(&data[0], offset + 8));

				float size = from_bytes<float>(&data[0], offset + 12);

				colliders.push_back({ position, size });
			}

			for (int offset = offset_bones; offset < offset_bones + amount_of_bones * 16; offset += 16)
			{
				bones.push_back
				(
					{
						glm::vec3(
							from_bytes<float>(&data[0], offset),
							from_bytes<float>(&data[0], offset + 4),
							from_bytes<float>(&data[0], offset + 8)
						)
					}
				);
			}
			*/

			// Parse colliders and bones from .mdl file (same for both TY 1 and TY 2)
			unsigned int collider_count = from_bytes<uint16_t>(data.data(), 8);
			unsigned int bone_count = from_bytes<uint16_t>(data.data(), 10);
			size_t collider_offset = from_bytes<uint32_t>(data.data(), 16);
			size_t bone_offset = from_bytes<uint32_t>(data.data(), 20);

			// Parse colliders
			for (unsigned int i = 0; i < collider_count; i++)
			{
				size_t offset = collider_offset + (i * 32);
				if (offset + 32 <= data.size())
				{
					glm::vec3 position(
						from_bytes<float>(data.data(), offset),
						from_bytes<float>(data.data(), offset + 4),
						from_bytes<float>(data.data(), offset + 8));

					float size = from_bytes<float>(data.data(), offset + 12);

					colliders.push_back({ position, size });
				}
			}

			// Parse bones
			for (unsigned int i = 0; i < bone_count; i++)
			{
				size_t offset = bone_offset + (i * 16);
				if (offset + 16 <= data.size())
				{
					bones.push_back(
						{
							glm::vec3(
								from_bytes<float>(data.data(), offset),
								from_bytes<float>(data.data(), offset + 4),
								from_bytes<float>(data.data(), offset + 8)
							)
						}
					);
				}
			}

			// Parse subobject bounds
			for (auto& subobj : mdl.subobjects)
			{
				bounds.push_back({
					glm::vec3(subobj.bounds.x, subobj.bounds.y, subobj.bounds.z),
					glm::vec3(subobj.bounds.sx, subobj.bounds.sy, subobj.bounds.sz)
				});
			}

			if (meshes.empty())
			{
				Debug::log("Warning: No meshes created for model: " + name);
			}
			else
			{
				Debug::log("Successfully created model: " + name + " with " + std::to_string(meshes.size()) + " meshes");
			}

			models[name] = new Model(meshes);
			models[name]->bounds_crn = glm::vec3(mdl.bounds.x, mdl.bounds.y, mdl.bounds.z);
			models[name]->bounds_size = glm::vec3(mdl.bounds.sx, mdl.bounds.sy, mdl.bounds.sz);

			models[name]->colliders = colliders;
			models[name]->bounds = bounds;
			models[name]->bones = bones;

			return models[name];
		}

	return NULL;
}

template<>
inline Font* Content::load(const std::string& name)
{
	if (archive == NULL)
	{
		Debug::log("Failed to load asset because no archive is loaded!");
		return NULL;
	}

	if (fonts.find(name) != fonts.end())
	{
		return fonts[name];
	}
	else
	{
		std::vector<char> data;
		if (archive->getFileData(name, data))
		{
			WFN fontInfo;
			fontInfo.load(data);

			std::unordered_map<char, FontRegion> regions;

			for (unsigned int i = 0; i < 256; i++)
			{
				if (fontInfo.regions[i].available)
				{
					regions[i].min[0] = fontInfo.regions[i].min[0];
					regions[i].min[1] = fontInfo.regions[i].min[1];

					regions[i].max[0] = fontInfo.regions[i].max[0];
					regions[i].max[1] = fontInfo.regions[i].max[1];

					regions[i].width = regions[i].max[0] - regions[i].min[0];
					regions[i].height = regions[i].max[1] - regions[i].min[1];

					regions[i].xAdvance = fontInfo.regions[i].xAdvance;
				}
			}

			std::string textureName = name.substr(0, name.find_last_of('.')) + ".wtx";

			fonts[name] = new Font(regions, load<Texture>(textureName), fontInfo.spaceWidth);

			return fonts[name];
		}
	}

	return NULL;
}