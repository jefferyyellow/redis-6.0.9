/* Hash Tables Implementation.
 *
 * This file implements in-memory hash tables with insert/del/replace/find/
 * get-random-element operations. Hash tables will auto-resize if needed
 * tables of power of two in size are used, collisions are handled by
 * chaining. See the source code for more information... :)
 *
 * Copyright (c) 2006-2012, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdint.h>

#ifndef __DICT_H
#define __DICT_H

#define DICT_OK 0
#define DICT_ERR 1

/* Unused arguments generate annoying warnings... */
#define DICT_NOTUSED(V) ((void) V)
// 字典中的条目
typedef struct dictEntry {
	// 键
    void *key;
	// 值
    union {
        void *val;
        uint64_t u64;
        int64_t s64;
        double d;
    } v;
	// 下一个
    struct dictEntry *next;
} dictEntry;

typedef struct dictType {
	// 对应的hash函数
    uint64_t (*hashFunction)(const void *key);
	// 键对应的复制函数
    void *(*keyDup)(void *privdata, const void *key);
	// 值对应的复制函数
    void *(*valDup)(void *privdata, const void *obj);
	// 键比较函数
    int (*keyCompare)(void *privdata, const void *key1, const void *key2);
	// 键销毁函数
    void (*keyDestructor)(void *privdata, void *key);
	// 值销毁函数
    void (*valDestructor)(void *privdata, void *obj);
} dictType;

/* This is our hash table structure. Every dictionary has two of this as we
 * implement incremental rehashing, for the old to the new table. */
// 这是我们的hash表结构体，每一个字典有两个这样的结构用于实现从老的到新的增量重新hash
typedef struct dictht {
    dictEntry **table;				// hash表
    unsigned long size;				// hash表大小
    unsigned long sizemask;			// 掩码 = size - 1
    unsigned long used;				// 已经使用
} dictht;

// hash字典
typedef struct dict {
    dictType *type;					// 该字典对应的特定操作函数
    void *privdata;					// 该字典依赖的数据,私有数据，配合type字段指向的函数一起使用
    dictht ht[2];					// 两个hash表
    long rehashidx; /* rehashing not in progress if rehashidx == -1 */ // rehash标识。默认值为-1，代表没有进行rehash操作
    unsigned long iterators; /* number of iterators currently running */ // 当前运行的迭代器数
} dict;

/* If safe is set to 1 this is a safe iterator, that means, you can call
 * dictAdd, dictFind, and other functions against the dictionary even while
 * iterating. Otherwise it is a non safe iterator, and only dictNext()
 * should be called while iterating. */
// 如果safe设置为1，则这是一个安全迭代器，当对字典迭代的时候可以调用dictAdd, dictFind和其他的函数
// 否则就是不安全的，当对字典迭代的时候只能调用dictNext
typedef struct dictIterator {
    dict *d;					// 对应的字典
    long index;					// 索引
    int table, safe;
    dictEntry *entry, *nextEntry;
    /* unsafe iterator fingerprint for misuse detection. */
	// 用于误用检测的不安全迭代器指纹
    long long fingerprint;
} dictIterator;

typedef void (dictScanFunction)(void *privdata, const dictEntry *de);
typedef void (dictScanBucketFunction)(void *privdata, dictEntry **bucketref);

/* This is the initial size of every hash table */
// 每个hash表的初始大小
#define DICT_HT_INITIAL_SIZE     4

/* ------------------------------- Macros ------------------------------------*/
// 销毁项的值，如果type中有值的销毁函数，调用销毁函数
#define dictFreeVal(d, entry) \
    if ((d)->type->valDestructor) \
        (d)->type->valDestructor((d)->privdata, (entry)->v.val)

// 设置项的新值,如果type中有值的复制函数，调用复制函数，否则直接赋值
#define dictSetVal(d, entry, _val_) do { \
    if ((d)->type->valDup) \
        (entry)->v.val = (d)->type->valDup((d)->privdata, _val_); \
    else \
        (entry)->v.val = (_val_); \
} while(0)

#define dictSetSignedIntegerVal(entry, _val_) \
    do { (entry)->v.s64 = _val_; } while(0)

#define dictSetUnsignedIntegerVal(entry, _val_) \
    do { (entry)->v.u64 = _val_; } while(0)

#define dictSetDoubleVal(entry, _val_) \
    do { (entry)->v.d = _val_; } while(0)

#define dictFreeKey(d, entry) \
    if ((d)->type->keyDestructor) \
        (d)->type->keyDestructor((d)->privdata, (entry)->key)

// 设置键，如果type有键的复制函数，优先调用，否则直接赋值
#define dictSetKey(d, entry, _key_) do { \
    if ((d)->type->keyDup) \
        (entry)->key = (d)->type->keyDup((d)->privdata, _key_); \
    else \
        (entry)->key = (_key_); \
} while(0)

// 比较两个键值，如果type中有比较函数，优先使用比较函数，或者使用直接比较
#define dictCompareKeys(d, key1, key2) \
    (((d)->type->keyCompare) ? \
        (d)->type->keyCompare((d)->privdata, key1, key2) : \
        (key1) == (key2))

#define dictHashKey(d, key) (d)->type->hashFunction(key)
#define dictGetKey(he) ((he)->key)
// 取得项对应的值
#define dictGetVal(he) ((he)->v.val)
#define dictGetSignedIntegerVal(he) ((he)->v.s64)
#define dictGetUnsignedIntegerVal(he) ((he)->v.u64)
#define dictGetDoubleVal(he) ((he)->v.d)
#define dictSlots(d) ((d)->ht[0].size+(d)->ht[1].size)
#define dictSize(d) ((d)->ht[0].used+(d)->ht[1].used)
// 检查字典是否在rehash中
#define dictIsRehashing(d) ((d)->rehashidx != -1)

/* API */
// 创建一个新的hash表
dict *dictCreate(dictType *type, void *privDataPtr);
// 扩展或者创建hash表
int dictExpand(dict *d, unsigned long size);
// 增加一个元素到目标hash表(如果存在，不处理）
int dictAdd(dict *d, void *key, void *val);
// 底层的增加或查找（如果是增加通过返回值返回，如果已经存在通过existing返回）
dictEntry *dictAddRaw(dict *d, void *key, dictEntry **existing);
// 增加或者搜索
dictEntry *dictAddOrFind(dict *d, void *key);
// 增加或者覆盖（设置key的值为val)
int dictReplace(dict *d, void *key, void *val);
// 搜索并且删除一个元素
int dictDelete(dict *d, const void *key);
// 从hash表中将元素删除，但是不实际释放键，值和字典项
dictEntry *dictUnlink(dict *ht, const void *key);
// 释放字典中的项
void dictFreeUnlinkedEntry(dict *d, dictEntry *he);
// 清空并且释放hash表
void dictRelease(dict *d);
// 找到键对应的项
dictEntry * dictFind(dict *d, const void *key);
// 取得键对应的值
void *dictFetchValue(dict *d, const void *key);
// 将hash表调整到包含所有元素最小大小
int dictResize(dict *d);
// 得到一个字典迭代器
dictIterator *dictGetIterator(dict *d);
// 得到一个字典安全迭代器
dictIterator *dictGetSafeIterator(dict *d);
// 迭代器的下一项
dictEntry *dictNext(dictIterator *iter);
// 释放迭代器
void dictReleaseIterator(dictIterator *iter);
dictEntry *dictGetRandomKey(dict *d);
dictEntry *dictGetFairRandomKey(dict *d);
unsigned int dictGetSomeKeys(dict *d, dictEntry **des, unsigned int count);
void dictGetStats(char *buf, size_t bufsize, dict *d);
uint64_t dictGenHashFunction(const void *key, int len);
uint64_t dictGenCaseHashFunction(const unsigned char *buf, int len);
void dictEmpty(dict *d, void(callback)(void*));
void dictEnableResize(void);
void dictDisableResize(void);
int dictRehash(dict *d, int n);
int dictRehashMilliseconds(dict *d, int ms);
void dictSetHashFunctionSeed(uint8_t *seed);
uint8_t *dictGetHashFunctionSeed(void);
unsigned long dictScan(dict *d, unsigned long v, dictScanFunction *fn, dictScanBucketFunction *bucketfn, void *privdata);
// 得到字典中键对应的hash值
uint64_t dictGetHash(dict *d, const void *key);
dictEntry **dictFindEntryRefByPtrAndHash(dict *d, const void *oldptr, uint64_t hash);

/* Hash table types */
extern dictType dictTypeHeapStringCopyKey;
extern dictType dictTypeHeapStrings;
extern dictType dictTypeHeapStringCopyKeyValue;

#endif /* __DICT_H */
