/*
 * Copyright (C) 2011 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ART_RUNTIME_MIRROR_OBJECT_H_
#define ART_RUNTIME_MIRROR_OBJECT_H_

#include "object_reference.h"
#include "offsets.h"
#include "verify_object.h"

namespace art {

class ImageWriter;
class LockWord;
class Monitor;
struct ObjectOffsets;
class Thread;
class VoidFunctor;

namespace mirror {

class ArtField;
class ArtMethod;
class Array;
class Class;
class FinalizerReference;
template<class T> class ObjectArray;
template<class T> class PrimitiveArray;
typedef PrimitiveArray<uint8_t> BooleanArray;
typedef PrimitiveArray<int8_t> ByteArray;
typedef PrimitiveArray<uint16_t> CharArray;
typedef PrimitiveArray<double> DoubleArray;
typedef PrimitiveArray<float> FloatArray;
typedef PrimitiveArray<int32_t> IntArray;
typedef PrimitiveArray<int64_t> LongArray;
typedef PrimitiveArray<int16_t> ShortArray;
class Reference;
class String;
class Throwable;

// Fields within mirror objects aren't accessed directly so that the appropriate amount of
// handshaking is done with GC (for example, read and write barriers). This macro is used to
// compute an offset for the Set/Get methods defined in Object that can safely access fields.
#define OFFSET_OF_OBJECT_MEMBER(type, field) \
    MemberOffset(OFFSETOF_MEMBER(type, field))

// Checks that we don't do field assignments which violate the typing system.
static constexpr bool kCheckFieldAssignments = false;

// C++ mirror of java.lang.Object
class MANAGED LOCKABLE Object {
 public:
  static MemberOffset ClassOffset() {
    return OFFSET_OF_OBJECT_MEMBER(Object, klass_);
  }

  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags,
           ReadBarrierOption kReadBarrierOption = kWithReadBarrier>
  Class* GetClass() ALWAYS_INLINE SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  void SetClass(Class* new_klass) SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  Object* GetReadBarrierPointer() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  void SetReadBarrierPointer(Object* rb_ptr) SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  bool AtomicSetReadBarrierPointer(Object* expected_rb_ptr, Object* rb_ptr)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  void AssertReadBarrierPointer() const SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  // The verifier treats all interfaces as java.lang.Object and relies on runtime checks in
  // invoke-interface to detect incompatible interface types.
  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  bool VerifierInstanceOf(Class* klass) SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  bool InstanceOf(Class* klass) SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags,
           ReadBarrierOption kReadBarrierOption = kWithReadBarrier>
  size_t SizeOf() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  Object* Clone(Thread* self) SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  int32_t IdentityHashCode() const SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  static MemberOffset MonitorOffset() {
    return OFFSET_OF_OBJECT_MEMBER(Object, monitor_);
  }

  // As volatile can be false if the mutators are suspended. This is an optimization since it
  // avoids the barriers.
  LockWord GetLockWord(bool as_volatile) SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  void SetLockWord(LockWord new_val, bool as_volatile) SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  // All Cas operations defined here have C++11 memory_order_seq_cst ordering
  // semantics: Preceding memory operations become visible to other threads
  // before the CAS, and subsequent operations become visible after the CAS.
  // The Cas operations defined here do not fail spuriously, i.e. they
  // have C++11 "strong" semantics.
  // TODO: In most, possibly all, cases, these assumptions are too strong.
  // Confirm and weaken the implementation.
  bool CasLockWord(LockWord old_val, LockWord new_val) SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  uint32_t GetLockOwnerThreadId();

  mirror::Object* MonitorEnter(Thread* self) SHARED_LOCKS_REQUIRED(Locks::mutator_lock_)
      EXCLUSIVE_LOCK_FUNCTION();
  bool MonitorExit(Thread* self) SHARED_LOCKS_REQUIRED(Locks::mutator_lock_)
      UNLOCK_FUNCTION();
  void Notify(Thread* self) SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  void NotifyAll(Thread* self) SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  void Wait(Thread* self) SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  void Wait(Thread* self, int64_t timeout, int32_t nanos) SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags,
           ReadBarrierOption kReadBarrierOption = kWithReadBarrier>
  bool IsClass() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags,
           ReadBarrierOption kReadBarrierOption = kWithReadBarrier>
  Class* AsClass() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  bool IsObjectArray() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  template<class T, VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  ObjectArray<T>* AsObjectArray() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags,
           ReadBarrierOption kReadBarrierOption = kWithReadBarrier>
  bool IsArrayInstance() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags,
           ReadBarrierOption kReadBarrierOption = kWithReadBarrier>
  Array* AsArray() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  BooleanArray* AsBooleanArray() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  ByteArray* AsByteArray() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  ByteArray* AsByteSizedArray() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  CharArray* AsCharArray() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  ShortArray* AsShortArray() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  ShortArray* AsShortSizedArray() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  IntArray* AsIntArray() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  LongArray* AsLongArray() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  FloatArray* AsFloatArray() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  DoubleArray* AsDoubleArray() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  String* AsString() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  Throwable* AsThrowable() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags,
           ReadBarrierOption kReadBarrierOption = kWithReadBarrier>
  bool IsArtMethod() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  ArtMethod* AsArtMethod() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags,
           ReadBarrierOption kReadBarrierOption = kWithReadBarrier>
  bool IsArtField() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  ArtField* AsArtField() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  bool IsReferenceInstance() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  Reference* AsReference() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  bool IsWeakReferenceInstance() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  bool IsSoftReferenceInstance() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  bool IsFinalizerReferenceInstance() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  FinalizerReference* AsFinalizerReference() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  bool IsPhantomReferenceInstance() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  // Accessor for Java type fields.
  template<class T, VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags,
      ReadBarrierOption kReadBarrierOption = kWithReadBarrier, bool kIsVolatile = false>
  T* GetFieldObject(MemberOffset field_offset) ALWAYS_INLINE
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  template<class T, VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags,
      ReadBarrierOption kReadBarrierOption = kWithReadBarrier>
  T* GetFieldObjectVolatile(MemberOffset field_offset) ALWAYS_INLINE
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  template<bool kTransactionActive, bool kCheckTransaction = true,
      VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags, bool kIsVolatile = false>
  void SetFieldObjectWithoutWriteBarrier(MemberOffset field_offset, Object* new_value)
      ALWAYS_INLINE SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  template<bool kTransactionActive, bool kCheckTransaction = true,
      VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags, bool kIsVolatile = false>
  void SetFieldObject(MemberOffset field_offset, Object* new_value) ALWAYS_INLINE
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  template<bool kTransactionActive, bool kCheckTransaction = true,
      VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  void SetFieldObjectVolatile(MemberOffset field_offset, Object* new_value) ALWAYS_INLINE
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  template<bool kTransactionActive, bool kCheckTransaction = true,
      VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  bool CasFieldObject(MemberOffset field_offset, Object* old_value, Object* new_value)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  HeapReference<Object>* GetFieldObjectReferenceAddr(MemberOffset field_offset);

  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags, bool kIsVolatile = false>
  int32_t GetField32(MemberOffset field_offset) ALWAYS_INLINE
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  int32_t GetField32Volatile(MemberOffset field_offset) ALWAYS_INLINE
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  template<bool kTransactionActive, bool kCheckTransaction = true,
      VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags, bool kIsVolatile = false>
  void SetField32(MemberOffset field_offset, int32_t new_value) ALWAYS_INLINE
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  template<bool kTransactionActive, bool kCheckTransaction = true,
      VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  void SetField32Volatile(MemberOffset field_offset, int32_t new_value) ALWAYS_INLINE
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  template<bool kTransactionActive, bool kCheckTransaction = true,
      VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  bool CasField32(MemberOffset field_offset, int32_t old_value, int32_t new_value) ALWAYS_INLINE
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags, bool kIsVolatile = false>
  int64_t GetField64(MemberOffset field_offset) ALWAYS_INLINE
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  int64_t GetField64Volatile(MemberOffset field_offset) ALWAYS_INLINE
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  template<bool kTransactionActive, bool kCheckTransaction = true,
      VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags, bool kIsVolatile = false>
  void SetField64(MemberOffset field_offset, int64_t new_value) ALWAYS_INLINE
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  template<bool kTransactionActive, bool kCheckTransaction = true,
      VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  void SetField64Volatile(MemberOffset field_offset, int64_t new_value) ALWAYS_INLINE
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  template<bool kTransactionActive, bool kCheckTransaction = true,
      VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  bool CasField64(MemberOffset field_offset, int64_t old_value, int64_t new_value)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  template<bool kTransactionActive, bool kCheckTransaction = true,
      VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags, typename T>
  void SetFieldPtr(MemberOffset field_offset, T new_value)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
#ifndef __LP64__
    SetField32<kTransactionActive, kCheckTransaction, kVerifyFlags>(
        field_offset, reinterpret_cast<int32_t>(new_value));
#else
    SetField64<kTransactionActive, kCheckTransaction, kVerifyFlags>(
        field_offset, reinterpret_cast<int64_t>(new_value));
#endif
  }

  // TODO fix thread safety analysis broken by the use of template. This should be
  // SHARED_LOCKS_REQUIRED(Locks::mutator_lock_).
  template <const bool kVisitClass, VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags,
      typename Visitor, typename JavaLangRefVisitor = VoidFunctor>
  void VisitReferences(const Visitor& visitor, const JavaLangRefVisitor& ref_visitor)
      NO_THREAD_SAFETY_ANALYSIS;

 protected:
  // Accessors for non-Java type fields
  template<class T, VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags, bool kIsVolatile = false>
  T GetFieldPtr(MemberOffset field_offset)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
#ifndef __LP64__
    return reinterpret_cast<T>(GetField32<kVerifyFlags, kIsVolatile>(field_offset));
#else
    return reinterpret_cast<T>(GetField64<kVerifyFlags, kIsVolatile>(field_offset));
#endif
  }

  // TODO: Fixme when anotatalysis works with visitors.
  template<bool kVisitClass, bool kIsStatic, typename Visitor>
  void VisitFieldsReferences(uint32_t ref_offsets, const Visitor& visitor) HOT_ATTR
      NO_THREAD_SAFETY_ANALYSIS;
  template<bool kVisitClass, typename Visitor>
  void VisitInstanceFieldsReferences(mirror::Class* klass, const Visitor& visitor) HOT_ATTR
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  template<bool kVisitClass, typename Visitor>
  void VisitStaticFieldsReferences(mirror::Class* klass, const Visitor& visitor) HOT_ATTR
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

 private:
  // Verify the type correctness of stores to fields.
  // TODO: This can cause thread suspension and isn't moving GC safe.
  void CheckFieldAssignmentImpl(MemberOffset field_offset, Object* new_value)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  void CheckFieldAssignment(MemberOffset field_offset, Object* new_value)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    if (kCheckFieldAssignments) {
      CheckFieldAssignmentImpl(field_offset, new_value);
    }
  }

  // Generate an identity hash code.
  static int32_t GenerateIdentityHashCode();

  // The Class representing the type of the object.
  HeapReference<Class> klass_;
  // Monitor and hash code information.
  uint32_t monitor_;

#ifdef USE_BAKER_OR_BROOKS_READ_BARRIER
  // Note names use a 'x' prefix and the x_rb_ptr_ is of type int
  // instead of Object to go with the alphabetical/by-type field order
  // on the Java side.
  uint32_t x_rb_ptr_;      // For the Baker or Brooks pointer.
  uint32_t x_xpadding_;    // For 8-byte alignment. TODO: get rid of this.
#endif

  friend class art::ImageWriter;
  friend class art::Monitor;
  friend struct art::ObjectOffsets;  // for verifying offset information
  DISALLOW_IMPLICIT_CONSTRUCTORS(Object);
};

}  // namespace mirror
}  // namespace art

#endif  // ART_RUNTIME_MIRROR_OBJECT_H_
