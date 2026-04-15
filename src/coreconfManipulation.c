/* CORECONF traversal, SID-path lookup, and subtree extraction helpers. */
#include "../include/coreconfManipulation.h"

#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#include "../include/coreconfTypes.h"
#include "../include/hashmap.h"
#include "../include/serialization.h"

#define PATH_MAX_LENGTH 100
#define MAX_STACK_SIZE 100
#define SID_KEY_SIZE 21
#define MAX_CORECONF_RECURSION_DEPTH 50


int clookupCompare(const void *a, const void *b, void *udata) {
    (void)udata;

    const CLookupT *clookup1 = a;
    const CLookupT *clookup2 = b;

    return (clookup1->childSID != clookup2->childSID);
}

uint64_t clookupHash(const void *item, uint64_t seed0, uint64_t seed1) {
    const CLookupT *clookup = (CLookupT *)item;
    return hashmap_murmur(&clookup->childSID, sizeof(uint64_t), seed0, seed1);
}


PathNodeT *createPathNode(int64_t parentSID, DynamicLongListT *sidKeys) {
    PathNodeT *pathNode = malloc(sizeof(PathNodeT));
    pathNode->parentSID = parentSID;
    pathNode->sidKeys = sidKeys;
    pathNode->nextPathNode = NULL;
    return pathNode;
}


PathNodeT *prependPathNode(PathNodeT *endNode, int64_t parentSID, DynamicLongListT *sidKeys) {
    PathNodeT *newPathNode = createPathNode(parentSID, sidKeys);
    newPathNode->nextPathNode = endNode;
    return newPathNode;
}


void printPathNode(PathNodeT *pathNode) {
    if (pathNode == NULL) {
        printf("PathNode is NULL\n");
        return;
    }
    int count = 0;
    PathNodeT *currentPathNode = pathNode;
    while (currentPathNode->parentSID != 0) {
        printf("parentSID = %" PRId64 " ", currentPathNode->parentSID);
        printDynamicLongList(currentPathNode->sidKeys);
        printf("\n");
        currentPathNode = currentPathNode->nextPathNode;
        count++;
    }

    if (count == 0)
        printf("parentSID = %" PRId64 " thus the given node is a parent node\n", currentPathNode->parentSID);
}


void freePathNode(PathNodeT *headNode) {
    PathNodeT *currentPathNode = headNode;
    PathNodeT *nextPathNode = NULL;
    while (currentPathNode != NULL) {
        nextPathNode = currentPathNode->nextPathNode;
        free(currentPathNode);
        currentPathNode = nextPathNode;
    }
}


PathNodeT *findRequirementForSID(uint64_t sid, struct hashmap *clookupHashmap, struct hashmap *keyMappingHashMap) {
    CLookupT *clookup = NULL;
    PathNodeT *pathNodes = createPathNode(0, NULL);

    const KeyMappingT *keyMappingForGivenSID = hashmap_get(keyMappingHashMap, &(KeyMappingT){.key = sid});
    if (keyMappingForGivenSID) {
        pathNodes = prependPathNode(pathNodes, sid, keyMappingForGivenSID->dynamicLongList);
    } else {
        pathNodes = prependPathNode(pathNodes, sid, NULL);
    }

    int64_t currentSID = sid;
    while (currentSID != 0) {
        clookup = (CLookupT *)hashmap_get(clookupHashmap, &(CLookupT){.childSID = currentSID});
        if (!clookup) {
            fprintf(stderr, "SID %" PRId64 " not found in the clookupHashmap\n", sid);
            return NULL;
        }

        int64_t parentSID = popLong(clookup->dynamicLongList);

        if (parentSID != 0) {
            const KeyMappingT *keyMapping = hashmap_get(keyMappingHashMap, &(KeyMappingT){.key = parentSID});
            if (keyMapping) {
                pathNodes = prependPathNode(pathNodes, parentSID, keyMapping->dynamicLongList);
            } else {
                pathNodes = prependPathNode(pathNodes, parentSID, NULL);
            }
        }

        currentSID = parentSID;
    }

    return pathNodes;
}

CoreconfValueT *examineCoreconfValue(CoreconfValueT *coreconfModel, DynamicLongListT *requestKeys,
                                     PathNodeT *headNode) {
    if (coreconfModel == NULL) {
        fprintf(stderr, "coreconfModel is NULL\n");
        return NULL;
    }

    if (headNode == NULL) {
        fprintf(stderr, "headNode is NULL\n");
        return NULL;
    }

    CoreconfValueT *subTree = coreconfModel;
    int64_t previousSID = 0;

    PathNodeT *currentPathNode = headNode;
    while (currentPathNode->parentSID != 0) {
        int64_t parentSID = currentPathNode->parentSID;
        DynamicLongListT *sidKeys = currentPathNode->sidKeys;

        currentPathNode = currentPathNode->nextPathNode;

        int64_t deltaSID = parentSID - previousSID;
        subTree = getCoreconfHashMap(subTree->data.map_value, deltaSID);
        previousSID = parentSID;

        if (sidKeys == NULL || sidKeys->size == 0) {
            continue;
        }

        if (subTree->type != CORECONF_ARRAY) {
            fprintf(stderr, "subTree is not a CoreconfArray\n");
            return NULL;
        }

        size_t arraySize = subTree->data.array_value->size;
        for (size_t i = 0; i < arraySize; i++) {
            CoreconfValueT *element = &subTree->data.array_value->elements[i];

            DynamicLongListT *requestKeysClone = malloc(sizeof(DynamicLongListT));
            cloneDynamicLongList(requestKeys, requestKeysClone);
            DynamicLongListT *sidKeyValueMatchDynamicLongList = malloc(sizeof(DynamicLongListT));
            initializeDynamicLongList(sidKeyValueMatchDynamicLongList);

            for (int i = 0; i < (int)sidKeys->size; i++) {
                uint64_t sidKey = sidKeys->longList[i];

                uint64_t sidDiff = sidKey - parentSID;
                CoreconfValueT *elementValueCheck = getCoreconfHashMap(element->data.map_value, sidDiff);
                uint64_t elementValueCheckInteger = getCoreconfValueAsUint64(elementValueCheck);

                uint64_t keyValueCheck = (uint64_t)popLong(requestKeysClone);
                if (elementValueCheckInteger == keyValueCheck)
                    addUniqueLong(sidKeyValueMatchDynamicLongList, (long)sidKey);
            }
            if (compareDynamicLongList(sidKeys, sidKeyValueMatchDynamicLongList)) {
                subTree = element;
                cloneDynamicLongList(requestKeysClone, requestKeys);
                break;
            }

        }
    }

    CoreconfValueT *returnMap = createCoreconfHashmap();
    insertCoreconfHashMap(returnMap->data.map_value, previousSID, subTree);
    return returnMap;
}

void buildCLookupHashmapFromCoreconf(CoreconfValueT *coreconfValue, struct hashmap *clookupHashmap, int64_t parentSID,
                                     int recursionDepth) {
    if (recursionDepth > MAX_CORECONF_RECURSION_DEPTH) return;

    if (coreconfValue->type == CORECONF_HASHMAP) {
        for (size_t i = 0; i < HASHMAP_TABLE_SIZE; i++) {
            CoreconfObjectT *current = coreconfValue->data.map_value->table[i];
            while (current != NULL) {
                uint64_t sidDiffValue = current->key;
                uint64_t childSIDValue = sidDiffValue + parentSID;

                CLookupT *clookup = (CLookupT *)hashmap_get(clookupHashmap, &(CLookupT){.childSID = childSIDValue});
                if (!clookup) {
                    clookup = malloc(sizeof(CLookupT));
                    clookup->childSID = childSIDValue;
                    clookup->dynamicLongList = malloc(sizeof(DynamicLongListT));
                    initializeDynamicLongList(clookup->dynamicLongList);
                    addUniqueLong(clookup->dynamicLongList, parentSID);
                    hashmap_set(clookupHashmap, clookup);
                } else {
                    addUniqueLong(clookup->dynamicLongList, parentSID);
                }
                buildCLookupHashmapFromCoreconf(current->value, clookupHashmap, childSIDValue, recursionDepth + 1);
                current = current->next;
            }
        }
    } else if (coreconfValue->type == CORECONF_ARRAY) {
        for (size_t i = 0; i < coreconfValue->data.array_value->size; i++) {
            CoreconfValueT *arrayElement = &coreconfValue->data.array_value->elements[i];
            buildCLookupHashmapFromCoreconf(arrayElement, clookupHashmap, parentSID, recursionDepth + 1);
        }
    } else {
    }
}


void printCLookupHashmap(struct hashmap *clookupHashmap) {
    size_t iter = 0;
    void *item;
    while (hashmap_iter(clookupHashmap, &iter, &item)) {
        CLookupT *clookupObject = item;
        printf("(Child SID =%lu) ", (long)clookupObject->childSID);
        printDynamicLongList(clookupObject->dynamicLongList);
    }
}

void long2str(char *stringValue, long longValue) { sprintf(stringValue, "%ld", longValue); }
