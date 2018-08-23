﻿//
//  SelectionScriptingInterface.cpp
//  interface/src/scripting
//
//  Created by Zach Fox on 2017-08-22.
//  Copyright 2017 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "SelectionScriptingInterface.h"
#include <QDebug>
#include "Application.h"

GameplayObjects::GameplayObjects() {
}

bool GameplayObjects::addToGameplayObjects(const QUuid& avatarID) {
    containsData = true;
    if (std::find(_avatarIDs.begin(), _avatarIDs.end(), avatarID) == _avatarIDs.end()) {
        _avatarIDs.push_back(avatarID);
    }
    return true;
}
bool GameplayObjects::removeFromGameplayObjects(const QUuid& avatarID) {
    _avatarIDs.erase(std::remove(_avatarIDs.begin(), _avatarIDs.end(), avatarID), _avatarIDs.end());
    return true;
}

bool GameplayObjects::addToGameplayObjects(const EntityItemID& entityID) {
    containsData = true;
    if (std::find(_entityIDs.begin(), _entityIDs.end(), entityID) == _entityIDs.end()) {
        _entityIDs.push_back(entityID);
    }
    return true;
}
bool GameplayObjects::removeFromGameplayObjects(const EntityItemID& entityID) {
    _entityIDs.erase(std::remove(_entityIDs.begin(), _entityIDs.end(), entityID), _entityIDs.end());
    return true;
}

bool GameplayObjects::addToGameplayObjects(const OverlayID& overlayID) {
    containsData = true;
    if (std::find(_overlayIDs.begin(), _overlayIDs.end(), overlayID) == _overlayIDs.end()) {
        _overlayIDs.push_back(overlayID);
    }
    return true;
}
bool GameplayObjects::removeFromGameplayObjects(const OverlayID& overlayID) {
    _overlayIDs.erase(std::remove(_overlayIDs.begin(), _overlayIDs.end(), overlayID), _overlayIDs.end());
    return true;
}


SelectionScriptingInterface::SelectionScriptingInterface() {
}

/**jsdoc
 * <table>
 *   <thead>
 *     <tr><th>Value</th><th>Description</th></tr>
 *   </thead>
 *   <tbody>
 *     <tr><td><code>"avatar"</code></td><td></td></tr>
 *     <tr><td><code>"entity"</code></td><td></td></tr>
 *     <tr><td><code>"overlay"</code></td><td></td></tr>
 *   </tbody>
 * </table>
 * @typedef {string} Selection.ItemType
 */
bool SelectionScriptingInterface::addToSelectedItemsList(const QString& listName, const QString& itemType, const QUuid& id) {
    if (itemType == "avatar") {
        return addToGameplayObjects(listName, (QUuid)id);
    } else if (itemType == "entity") {
        return addToGameplayObjects(listName, (EntityItemID)id);
    } else if (itemType == "overlay") {
        return addToGameplayObjects(listName, (OverlayID)id);
    }
    return false;
}
bool SelectionScriptingInterface::removeFromSelectedItemsList(const QString& listName, const QString& itemType, const QUuid& id) {
    if (itemType == "avatar") {
        return removeFromGameplayObjects(listName, (QUuid)id);
    } else if (itemType == "entity") {
        return removeFromGameplayObjects(listName, (EntityItemID)id);
    } else if (itemType == "overlay") {
        return removeFromGameplayObjects(listName, (OverlayID)id);
    }
    return false;
}

bool SelectionScriptingInterface::clearSelectedItemsList(const QString& listName) {
    {
        QWriteLocker lock(&_selectionListsLock);
        _selectedItemsListMap.insert(listName, GameplayObjects());
    }
    onSelectedItemsListChanged(listName);
    return true;
}

QStringList SelectionScriptingInterface::getListNames() const {
    QStringList list;
    QReadLocker lock(&_selectionListsLock);
    list = _selectedItemsListMap.keys();
    return list;
}

QStringList SelectionScriptingInterface::getHighlightedListNames() const {
    QStringList list;
    QReadLocker lock(&_highlightStylesLock);
    list = _highlightStyleMap.keys();
    return list;
}

bool SelectionScriptingInterface::enableListHighlight(const QString& listName, const QVariantMap& highlightStyleValues) {
    QWriteLocker lock(&_highlightStylesLock);

    auto highlightStyle = _highlightStyleMap.find(listName);
    if (highlightStyle == _highlightStyleMap.end()) {
        highlightStyle = _highlightStyleMap.insert(listName, SelectionHighlightStyle());

    }

    if (!(*highlightStyle).isBoundToList()) {
        enableListToScene(listName);
        (*highlightStyle).setBoundToList(true);
    }

    (*highlightStyle).fromVariantMap(highlightStyleValues);

    auto mainScene = qApp->getMain3DScene();
    if (mainScene) {
        render::Transaction transaction;
        transaction.resetSelectionHighlight(listName.toStdString(), (*highlightStyle).getStyle());
        mainScene->enqueueTransaction(transaction);
    }
    else {
        qWarning() << "SelectionToSceneHandler::highlightStyleChanged(), Unexpected null scene, possibly during application shutdown";
    }

    return true;
}

bool SelectionScriptingInterface::disableListHighlight(const QString& listName) {
    QWriteLocker lock(&_highlightStylesLock);
    auto highlightStyle = _highlightStyleMap.find(listName);
    if (highlightStyle != _highlightStyleMap.end()) {
        if ((*highlightStyle).isBoundToList()) {
            disableListToScene(listName);
        }

        _highlightStyleMap.erase(highlightStyle);

        auto mainScene = qApp->getMain3DScene();
        if (mainScene) {
            render::Transaction transaction;
            transaction.removeHighlightFromSelection(listName.toStdString());
            mainScene->enqueueTransaction(transaction);
        }
        else {
            qWarning() << "SelectionToSceneHandler::highlightStyleChanged(), Unexpected null scene, possibly during application shutdown";
        }
    }

    return true;
}

QVariantMap SelectionScriptingInterface::getListHighlightStyle(const QString& listName) const {
    QReadLocker lock(&_highlightStylesLock);
    auto highlightStyle = _highlightStyleMap.find(listName);
    if (highlightStyle == _highlightStyleMap.end()) {
        return QVariantMap();
    } else {
        return (*highlightStyle).toVariantMap();
    }
}

render::HighlightStyle SelectionScriptingInterface::getHighlightStyle(const QString& listName) const {
    QReadLocker lock(&_highlightStylesLock);
    auto highlightStyle = _highlightStyleMap.find(listName);
    if (highlightStyle == _highlightStyleMap.end()) {
        return render::HighlightStyle();
    } else {
        return (*highlightStyle).getStyle();
    }
}

bool SelectionScriptingInterface::enableListToScene(const QString& listName) {
    setupHandler(listName);

    return true;
}

bool SelectionScriptingInterface::disableListToScene(const QString& listName) {
    removeHandler(listName);

    return true;
}

template <class T> bool SelectionScriptingInterface::addToGameplayObjects(const QString& listName, T idToAdd) {
    {
        QWriteLocker lock(&_selectionListsLock);
        GameplayObjects currentList = _selectedItemsListMap.value(listName);
        currentList.addToGameplayObjects(idToAdd);
        _selectedItemsListMap.insert(listName, currentList);
    }
    onSelectedItemsListChanged(listName);
    return true;
}
template <class T> bool SelectionScriptingInterface::removeFromGameplayObjects(const QString& listName, T idToRemove) {
    bool listExist = false;
    {
        QWriteLocker lock(&_selectionListsLock);
        auto currentList = _selectedItemsListMap.find(listName);
        if (currentList != _selectedItemsListMap.end()) {
            listExist = true;
            (*currentList).removeFromGameplayObjects(idToRemove);
        }
    }
    if (listExist) {
        onSelectedItemsListChanged(listName);
        return true;
    }
    else {
        return false;
    }
}
//
// END HANDLING GENERIC ITEMS
//

GameplayObjects SelectionScriptingInterface::getList(const QString& listName) {
    QReadLocker lock(&_selectionListsLock);
    return _selectedItemsListMap.value(listName);
}

void SelectionScriptingInterface::printList(const QString& listName) {
    QReadLocker lock(&_selectionListsLock);
    auto currentList = _selectedItemsListMap.find(listName);
    if (currentList != _selectedItemsListMap.end()) {
        if ((*currentList).getContainsData()) {

            qDebug() << "List named " << listName << ":";
            qDebug() << "Avatar IDs:";
            for (auto i : (*currentList).getAvatarIDs()) {
                qDebug() << i << ';';
            }
            qDebug() << "";

            qDebug() << "Entity IDs:";
            for (auto j : (*currentList).getEntityIDs()) {
                qDebug() << j << ';';
            }
            qDebug() << "";

            qDebug() << "Overlay IDs:";
            for (auto k : (*currentList).getOverlayIDs()) {
                qDebug() << k << ';';
            }
            qDebug() << "";
        }
        else {
            qDebug() << "List named " << listName << " empty";
        }
    } else {
        qDebug() << "List named " << listName << " doesn't exist.";
    }
}

/**jsdoc
 * @typedef {object} Selection.SelectedItemsList
 * @property {Uuid[]} avatars - The IDs of the avatars in the selection.
 * @property {Uuid[]} entities - The IDs of the entities in the selection.
 * @property {Uuid[]} overlays - The IDs of the overlays in the selection.
 */
QVariantMap SelectionScriptingInterface::getSelectedItemsList(const QString& listName) const {
    QReadLocker lock(&_selectionListsLock);
    QVariantMap list;
    auto currentList = _selectedItemsListMap.find(listName);
    if (currentList != _selectedItemsListMap.end()) {
        QList<QVariant> avatarIDs;
        QList<QVariant> entityIDs;
        QList<QVariant> overlayIDs;

        if ((*currentList).getContainsData()) {
            if (!(*currentList).getAvatarIDs().empty()) {
                for (auto j : (*currentList).getAvatarIDs()) {
                    avatarIDs.push_back((QUuid)j);
                }
            }
            if (!(*currentList).getEntityIDs().empty()) {
                for (auto j : (*currentList).getEntityIDs()) {
                    entityIDs.push_back((QUuid)j );
                }
            }
            if (!(*currentList).getOverlayIDs().empty()) {
                for (auto j : (*currentList).getOverlayIDs()) {
                    overlayIDs.push_back((QUuid)j);
                }
            }
        }
        list["avatars"] = (avatarIDs);
        list["entities"] = (entityIDs);
        list["overlays"] = (overlayIDs);

        return list;
    }
    else {
        return list;
    }
}

bool SelectionScriptingInterface::removeListFromMap(const QString& listName) {
    bool removed = false;
    {
        QWriteLocker lock(&_selectionListsLock);
        removed = _selectedItemsListMap.remove(listName);
    }
    if (removed) {
        onSelectedItemsListChanged(listName);
        return true;
    } else {
        return false;
    }
}

void SelectionScriptingInterface::setupHandler(const QString& selectionName) {
    QWriteLocker lock(&_selectionHandlersLock);
    auto handler = _handlerMap.find(selectionName);
    if (handler == _handlerMap.end()) {
        handler = _handlerMap.insert(selectionName, new SelectionToSceneHandler());
    }

    (*handler)->initialize(selectionName);
}

void SelectionScriptingInterface::removeHandler(const QString& selectionName) {
    QWriteLocker lock(&_selectionHandlersLock);
    auto handler = _handlerMap.find(selectionName);
    if (handler != _handlerMap.end()) {
        delete handler.value();
        _handlerMap.erase(handler);
    }
}

void SelectionScriptingInterface::onSelectedItemsListChanged(const QString& listName) {
    {
        QWriteLocker lock(&_selectionHandlersLock);
        auto handler = _handlerMap.find(listName);
        if (handler != _handlerMap.end()) {
            (*handler)->updateSceneFromSelectedList();
        }
    }

    emit selectedItemsListChanged(listName);
}


SelectionToSceneHandler::SelectionToSceneHandler() {
}

void SelectionToSceneHandler::initialize(const QString& listName) {
    _listName = listName;
    updateSceneFromSelectedList();
}

void SelectionToSceneHandler::selectedItemsListChanged(const QString& listName) {
    if (listName == _listName) {
        updateSceneFromSelectedList();
    }
}

void SelectionToSceneHandler::updateSceneFromSelectedList() {
    auto mainScene = qApp->getMain3DScene();
    if (mainScene) {
        GameplayObjects thisList = DependencyManager::get<SelectionScriptingInterface>()->getList(_listName);
        render::Transaction transaction;
        render::ItemIDs finalList;
        render::ItemID currentID;
        auto entityTreeRenderer = DependencyManager::get<EntityTreeRenderer>();
        auto& overlays = qApp->getOverlays();

        for (QUuid& currentAvatarID : thisList.getAvatarIDs()) {
            auto avatar = std::static_pointer_cast<Avatar>(DependencyManager::get<AvatarManager>()->getAvatarBySessionID(currentAvatarID));
            if (avatar) {
                currentID = avatar->getRenderItemID();
                if (currentID != render::Item::INVALID_ITEM_ID) {
                    finalList.push_back(currentID);
                }
            }
        }

        for (EntityItemID& currentEntityID : thisList.getEntityIDs()) {
            currentID = entityTreeRenderer->renderableIdForEntityId(currentEntityID);
            if (currentID != render::Item::INVALID_ITEM_ID) {
                finalList.push_back(currentID);
            }
        }

        for (OverlayID& currentOverlayID : thisList.getOverlayIDs()) {
            auto overlay = overlays.getOverlay(currentOverlayID);
            if (overlay != NULL) {
                currentID = overlay->getRenderItemID();
                if (currentID != render::Item::INVALID_ITEM_ID) {
                    finalList.push_back(currentID);
                }
            }
        }

        render::Selection selection(_listName.toStdString(), finalList);
        transaction.resetSelection(selection);

        mainScene->enqueueTransaction(transaction);
    } else {
        qWarning() << "SelectionToSceneHandler::updateRendererSelectedList(), Unexpected null scene, possibly during application shutdown";
    }
}

bool SelectionHighlightStyle::fromVariantMap(const QVariantMap& properties) {
    auto colorVariant = properties["outlineUnoccludedColor"];
    if (colorVariant.isValid()) {
        bool isValid;
        auto color = vec3FromVariant(colorVariant, isValid);
        if (isValid) {
            _style._outlineUnoccluded.color = toGlm(color);
        }
    }
    colorVariant = properties["outlineOccludedColor"];
    if (colorVariant.isValid()) {
        bool isValid;
        auto color = vec3FromVariant(colorVariant, isValid);
        if (isValid) {
            _style._outlineOccluded.color = toGlm(color);
        }
    }
    colorVariant = properties["fillUnoccludedColor"];
    if (colorVariant.isValid()) {
        bool isValid;
        auto color = vec3FromVariant(colorVariant, isValid);
        if (isValid) {
            _style._fillUnoccluded.color = toGlm(color);
        }
    }
    colorVariant = properties["fillOccludedColor"];
    if (colorVariant.isValid()) {
        bool isValid;
        auto color = vec3FromVariant(colorVariant, isValid);
        if (isValid) {
            _style._fillOccluded.color = toGlm(color);
        }
    }

    auto intensityVariant = properties["outlineUnoccludedAlpha"];
    if (intensityVariant.isValid()) {
        _style._outlineUnoccluded.alpha = intensityVariant.toFloat();
    }
    intensityVariant = properties["outlineOccludedAlpha"];
    if (intensityVariant.isValid()) {
        _style._outlineOccluded.alpha = intensityVariant.toFloat();
    }
    intensityVariant = properties["fillUnoccludedAlpha"];
    if (intensityVariant.isValid()) {
        _style._fillUnoccluded.alpha = intensityVariant.toFloat();
    }
    intensityVariant = properties["fillOccludedAlpha"];
    if (intensityVariant.isValid()) {
        _style._fillOccluded.alpha = intensityVariant.toFloat();
    }

    auto outlineWidth = properties["outlineWidth"];
    if (outlineWidth.isValid()) {
        _style._outlineWidth = outlineWidth.toFloat();
    }
    auto isOutlineSmooth = properties["isOutlineSmooth"];
    if (isOutlineSmooth.isValid()) {
        _style._isOutlineSmooth = isOutlineSmooth.toBool();
    }

    return true;
}

/**jsdoc
 * @typedef {object} Selection.HighlightStyle
 * @property {Vec3Color} outlineUnoccludedColor - Color of the specified highlight region.
 * @property {Vec3Color} outlineOccludedColor - ""
 * @property {Vec3Color} fillUnoccludedColor- ""
 * @property {Vec3Color} fillOccludedColor- ""
 * @property {number} outlineUnoccludedAlpha - Alpha value ranging from <code>0.0</code> (not visible) to <code>1.0</code> 
 *     (fully opaque) for the specified highlight region.
 * @property {number} outlineOccludedAlpha - ""
 * @property {number} fillUnoccludedAlpha - ""
 * @property {number} fillOccludedAlpha - ""
 * @property {number} outlineWidth - Width of the outline, in pixels.
 * @property {boolean} isOutlineSmooth - <code>true</code> to enable outline smooth fall-off.
 */
QVariantMap SelectionHighlightStyle::toVariantMap() const {
    QVariantMap properties;

    const float MAX_COLOR = 255.0f;
    properties["outlineUnoccludedColor"] = vec3toVariant(_style._outlineUnoccluded.color * MAX_COLOR);
    properties["outlineOccludedColor"] = vec3toVariant(_style._outlineOccluded.color * MAX_COLOR);
    properties["fillUnoccludedColor"] = vec3toVariant(_style._fillUnoccluded.color * MAX_COLOR);
    properties["fillOccludedColor"] = vec3toVariant(_style._fillOccluded.color * MAX_COLOR);

    properties["outlineUnoccludedAlpha"] = _style._outlineUnoccluded.alpha;
    properties["outlineOccludedAlpha"] = _style._outlineOccluded.alpha;
    properties["fillUnoccludedAlpha"] = _style._fillUnoccluded.alpha;
    properties["fillOccludedAlpha"] = _style._fillOccluded.alpha;

    properties["outlineWidth"] = _style._outlineWidth;
    properties["isOutlineSmooth"] = _style._isOutlineSmooth;

    return properties;
}
