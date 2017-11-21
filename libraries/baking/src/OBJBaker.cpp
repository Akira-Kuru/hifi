//
//  OBJBaker.cpp
//  libraries/baking/src
//
//  Created by Utkarsh Gautam on 9/29/17.
//  Copyright 2017 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include <PathUtils.h>
#include <NetworkAccessManager.h>

#include "OBJBaker.h"
#include "OBJReader.h"
#include "FBXWriter.h"

const double UNIT_SCALE_FACTOR = 100.0;
const QByteArray PROPERTIES70_NODE_NAME = "Properties70";
const QByteArray P_NODE_NAME = "P";
const QByteArray C_NODE_NAME = "C";
const QByteArray FBX_HEADER_EXTENSION = "FBXHeaderExtension";
const QByteArray GLOBAL_SETTINGS_NODE_NAME = "GlobalSettings";
const QByteArray OBJECTS_NODE_NAME = "Objects";
const QByteArray GEOMETRY_NODE_NAME = "Geometry";
const QByteArray MODEL_NODE_NAME = "Model";
const QByteArray MATERIAL_NODE_NAME = "Material";
const QByteArray TEXTURE_NODE_NAME = "Texture";
const QByteArray TEXTURENAME_NODE_NAME = "TextureName";
const QByteArray RELATIVEFILENAME_NODE_NAME = "RelativeFilename";
const QByteArray CONNECTIONS_NODE_NAME = "Connections";
const QByteArray CONNECTIONS_NODE_PROPERTY = "OO";
const QByteArray CONNECTIONS_NODE_PROPERTY_1 = "OP";
const QByteArray MESH = "Mesh";

void OBJBaker::bake() {
    qDebug() << "OBJBaker" << _modelURL << "bake starting";

    // trigger bakeOBJ once OBJ is loaded
    connect(this, &OBJBaker::OBJLoaded, this, &OBJBaker::bakeOBJ);

    // make a local copy of the OBJ
    loadOBJ();
}

void OBJBaker::loadOBJ() {
    // check if the OBJ is local or it needs to be downloaded
    if (_modelURL.isLocalFile()) {
        // loading the local OBJ
        QFile localOBJ { _modelURL.toLocalFile() };

        qDebug() << "Local file url: " << _modelURL << _modelURL.toString() << _modelURL.toLocalFile() << ", copying to: " << _originalModelFilePath;

        if (!localOBJ.exists()) {
            handleError("Could not find " + _modelURL.toString());
            return;
        }

        // make a copy in the output folder
        if (!_originalOutputDir.isEmpty()) {
            qDebug() << "Copying to: " << _originalOutputDir << "/" << _modelURL.fileName();
            localOBJ.copy(_originalOutputDir + "/" + _modelURL.fileName());
        }

        localOBJ.copy(_originalModelFilePath);

        // local OBJ is loaded emit signal to trigger its baking
        emit OBJLoaded();
    } else {
        // OBJ is remote, start download 
        auto& networkAccessManager = NetworkAccessManager::getInstance();

        QNetworkRequest networkRequest;

        // setup the request to follow re-directs and always hit the network
        networkRequest.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
        networkRequest.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
        networkRequest.setHeader(QNetworkRequest::UserAgentHeader, HIGH_FIDELITY_USER_AGENT);
        networkRequest.setUrl(_modelURL);

        qCDebug(model_baking) << "Downloading" << _modelURL;
        auto networkReply = networkAccessManager.get(networkRequest);

        connect(networkReply, &QNetworkReply::finished, this, &OBJBaker::handleOBJNetworkReply);
    }
}

void OBJBaker::handleOBJNetworkReply() {
    auto requestReply = qobject_cast<QNetworkReply*>(sender());

    if (requestReply->error() == QNetworkReply::NoError) {
        qCDebug(model_baking) << "Downloaded" << _modelURL;

        // grab the contents of the reply and make a copy in the output folder
        QFile copyOfOriginal(_originalModelFilePath);

        qDebug(model_baking) << "Writing copy of original obj to" << _originalModelFilePath << copyOfOriginal.fileName();

        if (!copyOfOriginal.open(QIODevice::WriteOnly)) {
            // add an error to the error list for this obj stating that a duplicate of the original obj could not be made
            handleError("Could not create copy of " + _modelURL.toString() + " (Failed to open " + _originalModelFilePath + ")");
            return;
        }
        if (copyOfOriginal.write(requestReply->readAll()) == -1) {
            handleError("Could not create copy of " + _modelURL.toString() + " (Failed to write)");
            return;
        }

        // close that file now that we are done writing to it
        copyOfOriginal.close();

        if (!_originalOutputDir.isEmpty()) {
            copyOfOriginal.copy(_originalOutputDir + "/" + _modelURL.fileName());
        }

        // remote OBJ is loaded emit signal to trigger its baking
        emit OBJLoaded();
    } else {
        // add an error to our list stating that the OBJ could not be downloaded
        handleError("Failed to download " + _modelURL.toString());
    }
}

void OBJBaker::bakeOBJ() {
    // Read the OBJ file
    QFile objFile(_originalModelFilePath);
    if (!objFile.open(QIODevice::ReadOnly)) {
        handleError("Error opening " + _originalModelFilePath + " for reading");
        return;
    }

    QByteArray objData = objFile.readAll();

    bool combineParts = true; // set true so that OBJReader reads material info from material library
    OBJReader reader;
    FBXGeometry* geometry = reader.readOBJ(objData, QVariantHash(), combineParts, _modelURL);

    // Write OBJ Data as FBX tree nodes
    FBXNode rootNode;
    createFBXNodeTree(rootNode, *geometry);

    // Serialize the resultant FBX tree
    auto encodedFBX = FBXWriter::encodeFBX(rootNode);

    // Export as baked FBX
    auto fileName = _modelURL.fileName();
    auto baseName = fileName.left(fileName.lastIndexOf('.'));
    auto bakedFilename = baseName + ".baked.fbx";

    _bakedModelFilePath = _bakedOutputDir + "/" + bakedFilename;

    QFile bakedFile;
    bakedFile.setFileName(_bakedModelFilePath);
    if (!bakedFile.open(QIODevice::WriteOnly)) {
        handleError("Error opening " + _bakedModelFilePath + " for writing");
        return;
    }

    bakedFile.write(encodedFBX);

    // Export successful
    _outputFiles.push_back(_bakedModelFilePath);
    qCDebug(model_baking) << "Exported" << _modelURL << "to" << _bakedModelFilePath;

    // Export done emit finished
    emit finished();
}

void OBJBaker::createFBXNodeTree(FBXNode& rootNode, FBXGeometry& geometry) {
    // Generating FBX Header Node
    FBXNode headerNode;
    headerNode.name = FBX_HEADER_EXTENSION;

    // Generating global settings node
    // Required for Unit Scale Factor
    FBXNode globalSettingsNode;
    globalSettingsNode.name = GLOBAL_SETTINGS_NODE_NAME;

    // Setting the tree hierarchy: GlobalSettings -> Properties70 -> P -> Properties
    FBXNode properties70Node;
    properties70Node.name = PROPERTIES70_NODE_NAME;

    FBXNode pNode;
    {
        pNode.name = P_NODE_NAME;
        pNode.properties.append({
            "UnitScaleFactor", "double", "Number", "",
            UNIT_SCALE_FACTOR
        });
    }

    properties70Node.children = { pNode };
    globalSettingsNode.children = { properties70Node };

    // Generating Object node
    _objectNode.name = OBJECTS_NODE_NAME;

    // Generating Object node's child - Geometry node 
    FBXNode geometryNode;
    geometryNode.name = GEOMETRY_NODE_NAME;
    {
        _geometryID = _nodeID;
        geometryNode.properties = {
            _nodeID++,
            GEOMETRY_NODE_NAME,
            MESH
        };
    }

    // Compress the mesh information and store in dracoNode
    bool hasDeformers = false; // No concept of deformers for an OBJ
    FBXNode dracoNode;
    this->compressMesh(geometry.meshes[0], hasDeformers, dracoNode);
    geometryNode.children.append(dracoNode);

    // Generating Object node's child - Model node
    FBXNode modelNode;
    modelNode.name = MODEL_NODE_NAME;
    {
        _modelID = _nodeID++;
        modelNode.properties = {
            _nodeID,
            MODEL_NODE_NAME,
            MESH
        };
    }

    _objectNode.children = { geometryNode, modelNode };

    // Generating Objects node's child - Material node
    auto& meshParts = geometry.meshes[0].parts;
    for (auto& meshPart : meshParts) {
        FBXNode materialNode;
        materialNode.name = MATERIAL_NODE_NAME;
        if (geometry.materials.size() == 1) {
            // case when no material information is provided, OBJReader considers it as a single default material
            for (auto& materialID : geometry.materials.keys()) {
                setMaterialNodeProperties(materialNode, materialID, geometry);
            }
        } else {
            setMaterialNodeProperties(materialNode, meshPart.materialID, geometry);
        }

        _objectNode.children.append(materialNode);
    }

    // Generating Texture Node
    // iterate through mesh parts and process the associated textures
    auto size = meshParts.size();
    for (int i = 0; i < size; i++) {
        QString material = meshParts[i].materialID;
        FBXMaterial currentMaterial = geometry.materials[material];
        if (!currentMaterial.albedoTexture.filename.isEmpty() || !currentMaterial.specularTexture.filename.isEmpty()) {
            _textureID = _nodeID;
            _mapTextureMaterial.push_back(QPair<qlonglong, int>(_textureID, i));

            FBXNode textureNode;
            {
                textureNode.name = TEXTURE_NODE_NAME;
                textureNode.properties = { _nodeID++ };
            }

            // Texture node child - TextureName node
            FBXNode textureNameNode;
            {
                textureNameNode.name = TEXTURENAME_NODE_NAME;
                QByteArray propertyString = (!currentMaterial.albedoTexture.filename.isEmpty()) ? "Kd" : "Ka";
                textureNameNode.properties = { propertyString };
            }

            // Texture node child - Relative Filename node
            FBXNode relativeFilenameNode;
            {
                relativeFilenameNode.name = RELATIVEFILENAME_NODE_NAME;
            }

            QByteArray textureFileName = (!currentMaterial.albedoTexture.filename.isEmpty()) ? currentMaterial.albedoTexture.filename : currentMaterial.specularTexture.filename;

            // Callback to get Texture content and type
            getTextureTypeCallback textureContentTypeCallback = [=]() {
                return (!currentMaterial.albedoTexture.filename.isEmpty()) ? image::TextureUsage::Type::ALBEDO_TEXTURE : image::TextureUsage::Type::SPECULAR_TEXTURE;
            };

            // Compress the texture using ModelBaker::compressTexture() and store compressed file's name in the node
            QByteArray* textureFile = this->compressTexture(textureFileName, textureContentTypeCallback);
            if (!textureFile) {
                // Baking failed return
                return;
            }
            relativeFilenameNode.properties = { *textureFile };

            textureNode.children = { textureNameNode, relativeFilenameNode };

            _objectNode.children.append(textureNode);
        }
    }

    // Generating Connections node
    FBXNode connectionsNode;
    connectionsNode.name = CONNECTIONS_NODE_NAME;

    // connect Geometry to Model 
    FBXNode cNode;
    cNode.name = C_NODE_NAME;
    cNode.properties = {
        CONNECTIONS_NODE_PROPERTY,
        _geometryID,
        _modelID
    };
    connectionsNode.children = { cNode };

    // connect all materials to model
    for (auto& materialID : _materialIDs) {
        FBXNode cNode;
        cNode.name = C_NODE_NAME;
        cNode.properties = {
            CONNECTIONS_NODE_PROPERTY,
            materialID,
            _modelID
        };
        connectionsNode.children.append(cNode);
    }

    // Connect textures to materials
    for (auto& texMat : _mapTextureMaterial) {
        FBXNode cAmbientNode;
        cAmbientNode.name = C_NODE_NAME;
        cAmbientNode.properties = {
            CONNECTIONS_NODE_PROPERTY_1,
            texMat.first,
            _materialIDs[texMat.second],
            "AmbientFactor"
        };
        connectionsNode.children.append(cAmbientNode);

        FBXNode cDiffuseNode;
        cDiffuseNode.name = C_NODE_NAME;
        cDiffuseNode.properties = {
            CONNECTIONS_NODE_PROPERTY_1,
            texMat.first,
            _materialIDs[texMat.second],
            "DiffuseColor"
        };
        connectionsNode.children.append(cDiffuseNode);
    }

    // Make all generated nodes children of rootNode
    rootNode.children = { globalSettingsNode, _objectNode, connectionsNode };
}

// Set properties for material nodes
void OBJBaker::setMaterialNodeProperties(FBXNode& materialNode, QString material, FBXGeometry& geometry) {
    auto materialID = _nodeID++;
    _materialIDs.push_back(materialID);
    materialNode.properties = {
        materialID,
        material,
        MESH
    };

    FBXMaterial currentMaterial = geometry.materials[material];

    // Setting the hierarchy: Material -> Properties70 -> P -> Properties
    FBXNode properties70Node;
    properties70Node.name = PROPERTIES70_NODE_NAME;

    // Set diffuseColor
    FBXNode pNodeDiffuseColor;
    {
        pNodeDiffuseColor.name = P_NODE_NAME;
        pNodeDiffuseColor.properties.append({
            "DiffuseColor", "Color", "", "A",
            currentMaterial.diffuseColor[0], currentMaterial.diffuseColor[1], currentMaterial.diffuseColor[2]
        });
    }
    properties70Node.children.append(pNodeDiffuseColor);

    // Set specularColor
    FBXNode pNodeSpecularColor;
    {
        pNodeSpecularColor.name = P_NODE_NAME;
        pNodeSpecularColor.properties.append({
            "SpecularColor", "Color", "", "A",
            currentMaterial.specularColor[0], currentMaterial.specularColor[1], currentMaterial.specularColor[2]
        });
    }
    properties70Node.children.append(pNodeSpecularColor);

    // Set Shininess
    FBXNode pNodeShininess;
    {
        pNodeShininess.name = P_NODE_NAME;
        pNodeShininess.properties.append({
            "Shininess", "Number", "", "A",
            currentMaterial.shininess
        });
    }
    properties70Node.children.append(pNodeShininess);

    // Set Opacity
    FBXNode pNodeOpacity;
    {
        pNodeOpacity.name = P_NODE_NAME;
        pNodeOpacity.properties.append({
            "Opacity", "Number", "", "A",
            currentMaterial.opacity
        });
    }
    properties70Node.children.append(pNodeOpacity);

    materialNode.children.append(properties70Node);
}
