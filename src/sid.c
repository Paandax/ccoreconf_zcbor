/* SID utilities: dynamic lists, key mappings, and identifier lookup helpers. */
#include "../include/sid.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/hashmap.h"

#define SID_LENGTH 20
const char *SchemaIdentifierTypeStrings[] = {"String",
                                             "Unsigned Integer 8bit",
                                             "Unsigned Integer 16bit",
                                             "Unsigned Integer 32bit",
                                             "Unsigned Integer 64bit",
                                             "RCS Algorithm",
                                             "Decimal 64bit",
                                             "Boolean",
                                             "Identity Ref"};

DynamicLongListT *createDynamicLongList(void) {
    DynamicLongListT *dynamicLongList = (DynamicLongListT *)malloc(sizeof(DynamicLongListT));

    if (dynamicLongList == NULL) {
        fprintf(stderr, "Failed malloc'ing Dynamic long list");
        return NULL;
    }
    dynamicLongList->longList = NULL;
    dynamicLongList->size = 0;
    return dynamicLongList;
}

void initializeDynamicLongList(DynamicLongListT *dynamicLongList) {
    if (!dynamicLongList) return;
    dynamicLongList->longList = malloc(sizeof(long));
    dynamicLongList->size = 0;
}

int compareLong(const void *a, const void *b) { return (*(int *)a - *(int *)b); }

void sortDynamicLongList(DynamicLongListT *dynamicLongList, long sortedArray[]) {
    for (size_t i = 0; i < dynamicLongList->size; i++) {
        sortedArray[i] = dynamicLongList->longList[i];
    }

    qsort(sortedArray, dynamicLongList->size, sizeof(long), compareLong);
}

bool compareDynamicLongList(DynamicLongListT *dynamicLongList1, DynamicLongListT *dynamicLongList2) {
    size_t array1Size = dynamicLongList1->size;
    size_t array2Size = dynamicLongList2->size;

    if ((array1Size == 0) || (array2Size == 0)) return false;

    if (array1Size != array2Size) return false;

    long array1[array1Size];
    long array2[array1Size];

    sortDynamicLongList(dynamicLongList1, array1);
    sortDynamicLongList(dynamicLongList2, array2);

    for (size_t i = 0; i < array1Size; i++) {
        if (array1[i] != array2[i]) return false;
    }

    return true;
}

void addLong(DynamicLongListT *dynamicLongList, long value) {
    size_t currentListSize;
    if (dynamicLongList == NULL) {
        currentListSize = 0;
    } else {
        currentListSize = dynamicLongList->size;
    }

    dynamicLongList->longList = (long *)realloc(dynamicLongList->longList, (currentListSize + 1) * sizeof(long));
    if (!dynamicLongList->longList) {
        fprintf(stderr, "Failed realloc'ing long list");
        return;
    }
    dynamicLongList->size = currentListSize + 1;
    dynamicLongList->longList[currentListSize] = value;
}

long popLong(DynamicLongListT *dynamicLongList) {
    if (!dynamicLongList) return 0;
    if (dynamicLongList->size == 0) return 0;
    long lastValue = dynamicLongList->longList[dynamicLongList->size - 1];
    dynamicLongList->longList = (long *)realloc(dynamicLongList->longList, (dynamicLongList->size - 1) * sizeof(long));
    dynamicLongList->size = dynamicLongList->size - 1;
    return lastValue;
}

void cloneDynamicLongList(DynamicLongListT *originalDynamicLongList, DynamicLongListT *clonedDynamicLongList) {
    if (originalDynamicLongList == NULL || originalDynamicLongList->size == 0) return;

    initializeDynamicLongList(clonedDynamicLongList);
    for (size_t i = 0; i < originalDynamicLongList->size; i++) {
        addLong(clonedDynamicLongList, originalDynamicLongList->longList[i]);
    }
}

void addUniqueLong(DynamicLongListT *dynamicLongList, long value) {
    for (size_t i = 0; i < dynamicLongList->size; i++) {
        if (dynamicLongList->longList[i] == value) {
            return;
        }
    }
    addLong(dynamicLongList, value);
}

void printDynamicLongList(DynamicLongListT *dynamicLongList) {
    if (!dynamicLongList) {
        printf("[]");
        return;
    }
    printf("DLL: ");
    for (size_t i = 0; i < dynamicLongList->size; i++) {
        printf("%lu, ", dynamicLongList->longList[i]);
    }
    printf("\n");
}

void freeDynamicLongList(DynamicLongListT *dynamicLongList) {
    if (!dynamicLongList) return;
    free(dynamicLongList->longList);
    free(dynamicLongList);
}

int keyMappingCompare(const void *a, const void *b, void *udata) {
    (void)udata;

    const KeyMappingT *keyMapping1 = (KeyMappingT *)a;
    const KeyMappingT *keyMapping2 = (KeyMappingT *)b;
    return (keyMapping1->key != keyMapping2->key);
}

uint64_t keyMappingHash(const void *item, uint64_t seed0, uint64_t seed1) {
    const KeyMappingT *keyMapping = (KeyMappingT *)item;
    return hashmap_murmur(&(keyMapping->key), sizeof(uint64_t), seed0, seed1);
}

int identifierSIDCompare(const void *a, const void *b, void *udata) {
    (void)udata;

    const IdentifierSIDT *identifierSID1 = (IdentifierSIDT *)a;
    const IdentifierSIDT *identifierSID2 = (IdentifierSIDT *)b;
    return strcmp(identifierSID1->identifier, identifierSID2->identifier);
}

uint64_t identifierSIDHash(const void *item, uint64_t seed0, uint64_t seed1) {
    const IdentifierSIDT *identifierSID = (IdentifierSIDT *)item;
    return hashmap_sip(identifierSID->identifier, strlen(identifierSID->identifier), seed0, seed1);
}

int sidIdentifierCompare(const void *a, const void *b, void *udata) {
    (void)udata;

    const SIDIdentifierT *sidIdentifier1 = (SIDIdentifierT *)a;
    const SIDIdentifierT *sidIdentifier2 = (SIDIdentifierT *)b;
    return (sidIdentifier1->sid != sidIdentifier2->sid);
}

uint64_t sidIdentifierHash(const void *item, uint64_t seed0, uint64_t seed1) {
    const SIDIdentifierT *sidIdentifier = (SIDIdentifierT *)item;
    return hashmap_murmur(&(sidIdentifier->sid), sizeof(long), seed0, seed1);
}

int identifierTypeCompare(const void *a, const void *b, void *udata) {
    (void)udata;

    const IdentifierTypeT *identifierType1 = (IdentifierTypeT *)a;
    const IdentifierTypeT *identifierType2 = (IdentifierTypeT *)b;
    return strcmp(identifierType1->identifier, identifierType2->identifier);
}

uint64_t identifierTypeHash(const void *item, uint64_t seed0, uint64_t seed1) {
    const IdentifierTypeT *identifierType = (IdentifierTypeT *)item;
    return hashmap_sip(identifierType->identifier, strlen(identifierType->identifier), seed0, seed1);
}

void printKeyMappingT(const KeyMappingT *keyMapping) {
    printf("\nFor the key %d: \n", (int)keyMapping->key);

    for (size_t i = 0; i < keyMapping->dynamicLongList->size; i++) {
        long childSID = *(keyMapping->dynamicLongList->longList + i);
        printf("%lu, ", childSID);
    }
}

void printIdentifierSIDT(const IdentifierSIDT *identifierSID) {
    printf("\nIdentifier %s: %lu (SID) ", identifierSID->identifier, identifierSID->sid);
}

void printSIDIdentifierT(const SIDIdentifierT *sidIdentifier) {
    printf("\nSID %lu: %s (Identifier) ", sidIdentifier->sid, sidIdentifier->identifier);
}

void printIdentifierTypeT(const IdentifierTypeT *identifierType) {
    printf("\nIdentifier %s: %s (type) ", identifierType->identifier,
           SchemaIdentifierTypeStrings[identifierType->type]);
}

void printKeyMappingHashMap(struct hashmap *keyMappingHashMap) {
    size_t iter = 0;
    void *item;
    while (hashmap_iter(keyMappingHashMap, &iter, &item)) {
        const KeyMappingT *keyMapping = item;
        printKeyMappingT(keyMapping);
    }
}

void printHashMap(struct hashmap *anyHashMap, enum HashMapTypeEnum hashmapType) {
    size_t iter = 0;
    void *item;
    const KeyMappingT *keyMapping;
    const IdentifierSIDT *identifierSID;
    const SIDIdentifierT *sidIdentifier;
    const IdentifierTypeT *identifierType;

    while (hashmap_iter(anyHashMap, &iter, &item)) {
        switch (hashmapType) {
            case KEY_MAPPING:
                keyMapping = item;
                printKeyMappingT(keyMapping);
                break;
            case IDENTIFIER_SID:
                identifierSID = item;
                printIdentifierSIDT(identifierSID);
                break;
            case SID_IDENTIFIER:
                sidIdentifier = item;
                printSIDIdentifierT(sidIdentifier);
                break;
            case IDENTIFIER_TYPE:
                identifierType = item;
                printIdentifierTypeT(identifierType);
                break;
            default:
                fprintf(stderr, "Unknown Hashmap type");
                break;
        }
    }
}


int64_t char2int64(char *keyString) {
    intmax_t intValue = strtoimax(keyString, NULL, 10);
    if (intValue == INTMAX_MIN || intValue == INTMAX_MAX) {
        fprintf(stderr, "Conversion error or out of range");
        return INTMAX_MIN;
    }

    if (errno == ERANGE) {
        fprintf(stderr, "Value out of range");
        return INTMAX_MIN;
    }

    int64_t int64Value = (int64_t)intValue;

    return int64Value;
}

char *int2str(char *keyString, int64_t keyInt64) {
    int keyStringLength = snprintf(NULL, 0, "%d ", (int)keyInt64);
    if (keyStringLength < 0) {
        fprintf(stderr, "snprintf error");
        return NULL;
    }

    keyString = (char *)malloc((keyStringLength + 1) * sizeof(char));
    if (keyString == NULL) {
        fprintf(stderr, "Memory allocation failed");
        return NULL;
    }

    snprintf(keyString, keyStringLength + 1, "%d ", (int)keyInt64);
    return keyString;
}

void removeTrailingSlashFromPath(const char *qualifiedPath, char *formattedPath) {
    
    size_t len = strlen(qualifiedPath);

    if (qualifiedPath[len - 1] == '/') {
        strncpy(formattedPath, qualifiedPath, len - 1);
        formattedPath[len - 1] = '\0';  // Add null-terminator.

    } else {
        fprintf(stderr, "Wrong usage of formatPath function");
        strcpy(formattedPath, qualifiedPath);
    }
}

char *getSubstringAfterLastColon(const char *input) {
    char *lastColon = strrchr(input, ':');

    if (lastColon != NULL) {
        return lastColon + 1;
    } else {
        return (char *)input;
    }
}
