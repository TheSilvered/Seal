#include "sl_vm.h"
#include "clib_mem.h"

#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include <errno.h>

static void destroyObj(SlObj o);

SlSource slSourceFromCStr(const char *str) {
    size_t len = strlen(str);
    if (len > UINT32_MAX) {
        len = UINT32_MAX;
    }
    return (SlSource) {
        .path = "<string>",
        .text = (uint8_t *)str,
        .textLen = len
    };
}

SlSource *slSourceFromFile(SlVM *vm, const char *path) {
    SlSource *ret = NULL;
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        slSetError(vm, "failed to open %.1024s: %s", path, strerror(errno));
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        slSetError(vm, "failed to seek %.1024s: %s", path, strerror(errno));
        goto exit;
    }
    long fileSize = ftell(f);
    if (fileSize < 0) {
        slSetError(
            vm,
            "failed to get file size %.1024s: %s",
            path, strerror(errno)
        );
        goto exit;
    }
    if (fileSize > UINT32_MAX) {
        slSetError(vm, "file too big %.1024s, maximum size is 4GiB", path);
        goto exit;
    }
    assert(fseek(f, 0, SEEK_SET) == 0);

    ret = memAllocBytes(sizeof(*ret) + fileSize);
    if (ret == NULL) {
        slSetOutOfMemoryError(vm);
        goto exit;
    }
    ret->text = (uint8_t *)(ret + 1);
    ret->path = path;
    size_t textLen = fread(ret->text, 1, fileSize, f);
    if (textLen < fileSize && ferror(f) != 0 && !feof(f)) {
        int error = ferror(f);
        slSetError(
            vm,
            "failed to read file %.1024s: %s",
            path,
            strerror(error)
        );
        memFree(ret);
        ret = NULL;
        goto exit;
    }
    ret->textLen = textLen;

exit:
    fclose(f);
    return ret;
}

void slSourceFree(SlSource *source) {
    memFree(source);
}

SlObj slObjInt(int64_t value) {
    return (SlObj) {
        .type = SlObj_Int,
        .as.numInt = value
    };
}

SlObj slObjFloat(double value) {
    return (SlObj) {
        .type = SlObj_Int,
        .as.numFloat = value
    };
}

SlObj slFrozenStrNew(
    SlVM *vm,
    const uint8_t *bytes,
    size_t len
) {
    SlStr *str = memAllocBytes(sizeof(*str) + len * sizeof(*bytes));
    if (str == NULL) {
        slSetOutOfMemoryError(vm);
        return slNull;
    }

    str->asGCObj.refCount = 1;
    str->bytes = (uint8_t *)(str + 1);
    str->len = len;
    str->cap = 0;
    memcpy(str->bytes, bytes, len * sizeof(*bytes));

    return (SlObj){ .type = SlObj_FrozenStr, .as.str = str };
}

SlObj slFrozenStrFmt(SlVM *vm, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    size_t len = (size_t)vsnprintf(NULL, 0, fmt, args) + 1;

    SlStr *str = memAllocBytes(sizeof(*str) + len * sizeof(str->bytes));
    if (str == NULL) {
        slSetOutOfMemoryError(vm);
        va_end(args);
        return slNull;
    }

    str->asGCObj.refCount = 1;
    str->bytes = (uint8_t *)(str + 1);
    str->len = len - 1;
    str->cap = 0;

    (void)vsnprintf((char *)str->bytes, len, fmt, args);
    va_end(args);

    return (SlObj){ .type = SlObj_FrozenStr, .as.str = str };
}

SlObj slPrototypeNew(
    SlVM *vm,
    uint8_t *bytes,
    uint32_t size,
    SlObj *constants,
    uint32_t constCount,
    SlSharedInfo *sharedInfo,
    uint16_t sharedCount,
    uint16_t frameSize,
    SlDebugInfo *debugInfo
) {
    SlPrototype *proto = memAllocBytes(sizeof(*proto));

    if (proto == NULL) {
        slSetOutOfMemoryError(vm);
        memFree(bytes);
        for (uint32_t i = 0; i < constCount; i++) {
            slDelRef(constants[i]);
        }
        memFree(constants);
        memFree(sharedInfo);
        return slNull;
    }

    proto->asGCObj.refCount = 1;
    proto->bytes = bytes;
    proto->size = size;
    proto->constants = constants;
    proto->constCount = constCount;
    proto->sharedInfo = sharedInfo;
    proto->sharedCount = sharedCount;
    proto->frameSize = frameSize;
    proto->debugInfo = debugInfo;

    return (SlObj){ .type = SlObj_Prototype, .as.proto = proto };
}

SlObj slNewRef(SlObj obj) {
    if (!slObjIsSmall(obj)) {
        obj.as.gcObj->refCount++;
    }
    return obj;
}

void slDelRef(SlObj obj) {
    if (!slObjIsSmall(obj)) {
        obj.as.gcObj->refCount--;
        if (obj.as.gcObj->refCount == 0) {
            destroyObj(obj);
        }
    }
}

const char *slTypeName(SlObj o) {
    switch ((SlObjType)o.type) {
    case SlObj_Null:
        return "Null";
    case SlObj_Empty:
        return "<internal:Empty>";
    case SlObj_StackIdx:
        return "<internal:StackIdx>";
    case SlObj_Bool:
        return "Bool";
    case SlObj_Int:
        return "Int";
    case SlObj_Float:
        return "Float";
    case SlObj_Str:
        return "Str";
    case SlObj_Prototype:
        return "<internal:Prototype>";
    case SlObj_List:
        return "List";
    case SlObj_Map:
        return "Map";
    case SlObj_Func:
        return "Func";
    case SlObj_Struct:
        return "Struct";
    case SlObj_SharedSlot:
        return "<internal:SharedSlot>";
    case SlObj_FrozenStr:
        return "Str*";
    case SlObj_FrozenList:
        return "List*";
    case SlObj_FrozenMap:
        return "Map*";
    }
    assert(false && "unreachable");
}

void slSetOutOfMemoryError(SlVM *vm) {
    const char msg[] = "Out of memory.";
    memcpy(vm->error.msg, msg, sizeof(msg));
    vm->error.occurred = true;
}

void slSetError(SlVM *vm, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(vm->error.msg, sizeof(vm->error.msg), fmt, args);
    va_end(args);
    vm->error.occurred = true;
}

void slSetErrorVArg(SlVM *vm, const char *fmt, va_list args) {
    vsnprintf(vm->error.msg, sizeof(vm->error.msg), fmt, args);
    vm->error.occurred = true;
}

static void delPtrRef(SlObjType type, SlGCObj *obj) {
    slDelRef((SlObj){ .type = type, .as.gcObj = obj });
}

static void destroyObj(SlObj o) {
    switch ((SlObjType)(o.type & 0xff)) {
    case SlObj_Null:
    case SlObj_Empty:
    case SlObj_Bool:
    case SlObj_Int:
    case SlObj_Float:
    case SlObj_StackIdx:
        break;
    case SlObj_Str:
        if (o.as.str->cap != 0) {
            memFree(o.as.str->bytes);
        }
        memFree(o.as.str);
        break;
    case SlObj_Prototype:
        o.as.gcObj->refCount = SIZE_MAX;
        for (uint32_t i = 0; i < o.as.proto->constCount; i++) {
            slDelRef(o.as.proto->constants[i]);
        }
        if (o.as.proto->debugInfo != NULL) {
            SlDebugInfo *debugInfo = o.as.proto->debugInfo;
            memFree(debugInfo->lineInfo);
            memFree(debugInfo->slotInfo);
            memFree(debugInfo);
        }

        memFree(o.as.proto->bytes);
        memFree(o.as.proto->constants);
        memFree(o.as.proto->sharedInfo);
        memFree(o.as.proto);
        break;
    case SlObj_List:
        o.as.gcObj->refCount = SIZE_MAX;
        for (size_t i = 0; i < o.as.list->len; i++) {
            slDelRef(o.as.list->objs[i]);
        }
        if (o.as.list->cap != 0) {
            memFree(o.as.list->objs);
        }
        memFree(o.as.list);
        break;
    case SlObj_Map:
        o.as.gcObj->refCount = SIZE_MAX;
        for (size_t i = 0; i < o.as.map->cap; i++) {
            slDelRef(o.as.map->entries[i].key);
            slDelRef(o.as.map->entries[i].value);
        }
        memFree(o.as.map->entries);
        memFree(o.as.map);
        break;
    case SlObj_Func:
        o.as.gcObj->refCount = SIZE_MAX;
        for (uint16_t i = 0; i < o.as.func->proto->sharedCount; i++) {
            delPtrRef(
                SlObj_SharedSlot,
                &o.as.func->sharedSlots[i]->asGCObj
            );
        }
        memFree(o.as.func);
        break;
    case SlObj_Struct:
        o.as.gcObj->refCount = SIZE_MAX;
        if (o.as.structure->mt != NULL
            && o.as.structure->mt->destructor != NULL
        ) {
            o.as.structure->mt->destructor(o.as.structure);
        }
        memFree(o.as.structure);
        break;
    case SlObj_SharedSlot:
        o.as.gcObj->refCount = SIZE_MAX;
        slDelRef(o.as.sharedSlot->value);
        memFree(o.as.sharedSlot);
        break;

    case SlObj_FrozenStr:
    case SlObj_FrozenList:
    case SlObj_FrozenMap:
        assert(false && "unreachable");
    }
}
