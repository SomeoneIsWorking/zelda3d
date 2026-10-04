#include "fast/resource/factory/VertexFactory.h"
#include "fast/resource/type/Vertex.h"
#include "spdlog/spdlog.h"
#include "libultraship/libultra/gbi.h"
#include <tinyxml2.h>

namespace Fast {
std::shared_ptr<Ship::IResource>
ResourceFactoryBinaryVertexV0::ReadResource(std::shared_ptr<Ship::File> file,
                                            std::shared_ptr<Ship::ResourceInitData> initData) {
    if (!FileHasValidFormatAndReader(file, initData)) {
        return nullptr;
    }

    auto vertex = std::make_shared<Vertex>(initData);
    auto reader = std::get<std::shared_ptr<Ship::BinaryReader>>(file->Reader);

    uint32_t count = reader->ReadUInt32();
    vertex->VertexList.reserve(count);

    for (uint32_t i = 0; i < count; i++) {
        Vtx data;
        data.v.ob[0] = reader->ReadInt16();
        data.v.ob[1] = reader->ReadInt16();
        data.v.ob[2] = reader->ReadInt16();
        data.v.flag = reader->ReadUInt16();
        data.v.tc[0] = reader->ReadInt16();
        data.v.tc[1] = reader->ReadInt16();
        data.v.cn[0] = reader->ReadUByte();
        data.v.cn[1] = reader->ReadUByte();
        data.v.cn[2] = reader->ReadUByte();
        data.v.cn[3] = reader->ReadUByte();
        vertex->VertexList.push_back(data);
    }

    return vertex;
}

std::shared_ptr<Ship::IResource>
ResourceFactoryXMLVertexV0::ReadResource(std::shared_ptr<Ship::File> file,
                                         std::shared_ptr<Ship::ResourceInitData> initData) {
    if (!FileHasValidFormatAndReader(file, initData)) {
        return nullptr;
    }

    // The root element is what ReadResourceInitDataXml keyed the resource type off, but a document
    // can still reach here with nothing under it, so it is checked rather than dereferenced. An
    // element with no children is not rejected: the loop below simply emits no vertices for it,
    // which is the behaviour an empty (but well formed) vertex list already had.
    auto document = std::get<std::shared_ptr<tinyxml2::XMLDocument>>(file->Reader);
    auto root = document != nullptr ? document->FirstChildElement() : nullptr;
    if (root == nullptr) {
        SPDLOG_ERROR("Failed to load resource: XML Vertex at {} has no root element", initData->Path);
        return nullptr;
    }

    auto vertex = std::make_shared<Vertex>(initData);
    auto child = root->FirstChildElement();

    while (child != nullptr) {
        std::string childName = child->Name();

        if (childName == "Vtx") {
            Vtx data;
            data.v.ob[0] = child->IntAttribute("X");
            data.v.ob[1] = child->IntAttribute("Y");
            data.v.ob[2] = child->IntAttribute("Z");
            data.v.flag = 0;
            data.v.tc[0] = child->IntAttribute("S");
            data.v.tc[1] = child->IntAttribute("T");
            data.v.cn[0] = child->IntAttribute("R");
            data.v.cn[1] = child->IntAttribute("G");
            data.v.cn[2] = child->IntAttribute("B");
            data.v.cn[3] = child->IntAttribute("A");

            vertex->VertexList.push_back(data);
        }

        child = child->NextSiblingElement();
    }

    return vertex;
}
} // namespace Fast
