/*
 * Copyright (C) 2008 The Android Open Source Project
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

#include <algorithm>
#include <set>
#include <fcntl.h>
#ifdef __linux__
#include <sys/sendfile.h>
#else
#include <sys/socket.h>
#endif
#include <sys/stat.h>
#include <unistd.h>

#include "base/logging.h"
#include "base/stl_util.h"
#include "class_linker.h"
#include "common_throws.h"
#include "dex_file-inl.h"
#include "gc/space/image_space.h"
#include "gc/space/space-inl.h"
#include "image.h"
#include "jni_internal.h"
#include "mirror/class_loader.h"
#include "mirror/object-inl.h"
#include "mirror/string.h"
#include "oat.h"
#include "os.h"
#include "profiler.h"
#include "runtime.h"
#include "scoped_thread_state_change.h"
#include "ScopedFd.h"
#include "ScopedLocalRef.h"
#include "ScopedUtfChars.h"
#include "utils.h"
#include "well_known_classes.h"
#include "zip_archive.h"

namespace art {

// A smart pointer that provides read-only access to a Java string's UTF chars.
// Unlike libcore's NullableScopedUtfChars, this will *not* throw NullPointerException if
// passed a null jstring. The correct idiom is:
//
//   NullableScopedUtfChars name(env, javaName);
//   if (env->ExceptionCheck()) {
//       return NULL;
//   }
//   // ... use name.c_str()
//
// TODO: rewrite to get rid of this, or change ScopedUtfChars to offer this option.
class NullableScopedUtfChars {
 public:
  NullableScopedUtfChars(JNIEnv* env, jstring s) : mEnv(env), mString(s) {
    mUtfChars = (s != NULL) ? env->GetStringUTFChars(s, NULL) : NULL;
  }

  ~NullableScopedUtfChars() {
    if (mUtfChars) {
      mEnv->ReleaseStringUTFChars(mString, mUtfChars);
    }
  }

  const char* c_str() const {
    return mUtfChars;
  }

  size_t size() const {
    return strlen(mUtfChars);
  }

  // Element access.
  const char& operator[](size_t n) const {
    return mUtfChars[n];
  }

 private:
  JNIEnv* mEnv;
  jstring mString;
  const char* mUtfChars;

  // Disallow copy and assignment.
  NullableScopedUtfChars(const NullableScopedUtfChars&);
  void operator=(const NullableScopedUtfChars&);
};

static jlong DexFile_openDexFileNative(JNIEnv* env, jclass, jstring javaSourceName, jstring javaOutputName, jint) {
  ScopedUtfChars sourceName(env, javaSourceName);
  if (sourceName.c_str() == NULL) {
    return 0;
  }
  NullableScopedUtfChars outputName(env, javaOutputName);
  if (env->ExceptionCheck()) {
    return 0;
  }

  ClassLinker* linker = Runtime::Current()->GetClassLinker();
  std::unique_ptr<std::vector<const DexFile*>> dex_files(new std::vector<const DexFile*>());
  std::vector<std::string> error_msgs;

  bool success = linker->OpenDexFilesFromOat(sourceName.c_str(), outputName.c_str(), &error_msgs,
                                             dex_files.get());

  if (success) {
    return static_cast<jlong>(reinterpret_cast<uintptr_t>(dex_files.release()));
  } else {
    // The vector should be empty after a failed loading attempt.
    DCHECK_EQ(0U, dex_files->size());

    ScopedObjectAccess soa(env);
    CHECK(!error_msgs.empty());
    // The most important message is at the end. So set up nesting by going forward, which will
    // wrap the existing exception as a cause for the following one.
    auto it = error_msgs.begin();
    auto itEnd = error_msgs.end();
    for ( ; it != itEnd; ++it) {
      ThrowWrappedIOException("%s", it->c_str());
    }

    return 0;
  }
}

static std::vector<const DexFile*>* toDexFiles(jlong dex_file_address, JNIEnv* env) {
  std::vector<const DexFile*>* dex_files = reinterpret_cast<std::vector<const DexFile*>*>(
      static_cast<uintptr_t>(dex_file_address));
  if (UNLIKELY(dex_files == nullptr)) {
    ScopedObjectAccess soa(env);
    ThrowNullPointerException(NULL, "dex_file == null");
  }
  return dex_files;
}

static void DexFile_closeDexFile(JNIEnv* env, jclass, jlong cookie) {
  std::unique_ptr<std::vector<const DexFile*>> dex_files(toDexFiles(cookie, env));
  if (dex_files.get() == nullptr) {
    return;
  }
  ScopedObjectAccess soa(env);

  size_t index = 0;
  for (const DexFile* dex_file : *dex_files) {
    if (Runtime::Current()->GetClassLinker()->IsDexFileRegistered(*dex_file)) {
      (*dex_files)[index] = nullptr;
    }
    index++;
  }

  STLDeleteElements(dex_files.get());
  // Unique_ptr will delete the vector itself.
}

static jclass DexFile_defineClassNative(JNIEnv* env, jclass, jstring javaName, jobject javaLoader,
                                        jlong cookie) {
  std::vector<const DexFile*>* dex_files = toDexFiles(cookie, env);
  if (dex_files == NULL) {
    VLOG(class_linker) << "Failed to find dex_file";
    return NULL;
  }
  ScopedUtfChars class_name(env, javaName);
  if (class_name.c_str() == NULL) {
    VLOG(class_linker) << "Failed to find class_name";
    return NULL;
  }
  const std::string descriptor(DotToDescriptor(class_name.c_str()));

  for (const DexFile* dex_file : *dex_files) {
    const DexFile::ClassDef* dex_class_def = dex_file->FindClassDef(descriptor.c_str());
    if (dex_class_def != nullptr) {
      ScopedObjectAccess soa(env);
      ClassLinker* class_linker = Runtime::Current()->GetClassLinker();
      class_linker->RegisterDexFile(*dex_file);
      StackHandleScope<1> hs(soa.Self());
      Handle<mirror::ClassLoader> class_loader(
          hs.NewHandle(soa.Decode<mirror::ClassLoader*>(javaLoader)));
      mirror::Class* result = class_linker->DefineClass(descriptor.c_str(), class_loader, *dex_file,
                                                        *dex_class_def);
      if (result != nullptr) {
        VLOG(class_linker) << "DexFile_defineClassNative returning " << result;
        return soa.AddLocalReference<jclass>(result);
      }
    }
  }
  VLOG(class_linker) << "Failed to find dex_class_def";
  return nullptr;
}

// Needed as a compare functor for sets of const char
struct CharPointerComparator {
  bool operator()(const char *str1, const char *str2) const {
    return strcmp(str1, str2) < 0;
  }
};

// Note: this can be an expensive call, as we sort out duplicates in MultiDex files.
static jobjectArray DexFile_getClassNameList(JNIEnv* env, jclass, jlong cookie) {
  jobjectArray result = nullptr;
  std::vector<const DexFile*>* dex_files = toDexFiles(cookie, env);

  if (dex_files != nullptr) {
    // Push all class descriptors into a set. Use set instead of unordered_set as we want to
    // retrieve all in the end.
    std::set<const char*, CharPointerComparator> descriptors;
    for (const DexFile* dex_file : *dex_files) {
      for (size_t i = 0; i < dex_file->NumClassDefs(); ++i) {
        const DexFile::ClassDef& class_def = dex_file->GetClassDef(i);
        const char* descriptor = dex_file->GetClassDescriptor(class_def);
        descriptors.insert(descriptor);
      }
    }

    // Now create output array and copy the set into it.
    result = env->NewObjectArray(descriptors.size(), WellKnownClasses::java_lang_String, nullptr);
    if (result != nullptr) {
      auto it = descriptors.begin();
      auto it_end = descriptors.end();
      jsize i = 0;
      for (; it != it_end; it++, ++i) {
        std::string descriptor(DescriptorToDot(*it));
        ScopedLocalRef<jstring> jdescriptor(env, env->NewStringUTF(descriptor.c_str()));
        if (jdescriptor.get() == nullptr) {
          return nullptr;
        }
        env->SetObjectArrayElement(result, i, jdescriptor.get());
      }
    }
  }
  return result;
}

static void CopyProfileFile(const char* oldfile, const char* newfile) {
  ScopedFd src(open(oldfile, O_RDONLY));
  if (src.get() == -1) {
    PLOG(ERROR) << "Failed to open profile file " << oldfile
      << ". My uid:gid is " << getuid() << ":" << getgid();
    return;
  }

  struct stat stat_src;
  if (fstat(src.get(), &stat_src) == -1) {
    PLOG(ERROR) << "Failed to get stats for profile file  " << oldfile
      << ". My uid:gid is " << getuid() << ":" << getgid();
    return;
  }

  // Create the copy with rw------- (only accessible by system)
  ScopedFd dst(open(newfile, O_WRONLY|O_CREAT|O_TRUNC, 0600));
  if (dst.get()  == -1) {
    PLOG(ERROR) << "Failed to create/write prev profile file " << newfile
      << ".  My uid:gid is " << getuid() << ":" << getgid();
    return;
  }

#ifdef __linux__
  if (sendfile(dst.get(), src.get(), nullptr, stat_src.st_size) == -1) {
#else
  off_t len;
  if (sendfile(dst.get(), src.get(), 0, &len, nullptr, 0) == -1) {
#endif
    PLOG(ERROR) << "Failed to copy profile file " << oldfile << " to " << newfile
      << ". My uid:gid is " << getuid() << ":" << getgid();
  }
}

static jboolean IsDexOptNeededInternal(JNIEnv* env, const char* filename,
    const char* pkgname, const char* instruction_set, const jboolean defer) {
  const bool kVerboseLogging = false;  // Spammy logging.
  const bool kReasonLogging = true;  // Logging of reason for returning JNI_TRUE.

  if ((filename == nullptr) || !OS::FileExists(filename)) {
    LOG(ERROR) << "DexFile_isDexOptNeeded file '" << filename << "' does not exist";
    ScopedLocalRef<jclass> fnfe(env, env->FindClass("java/io/FileNotFoundException"));
    const char* message = (filename == nullptr) ? "<empty file name>" : filename;
    env->ThrowNew(fnfe.get(), message);
    return JNI_FALSE;
  }

  // Always treat elements of the bootclasspath as up-to-date.  The
  // fact that code is running at all means that this should be true.
  Runtime* runtime = Runtime::Current();
  ClassLinker* class_linker = runtime->GetClassLinker();
  // TODO: We're assuming that the 64 and 32 bit runtimes have identical
  // class paths. isDexOptNeeded will not necessarily be called on a runtime
  // that has the same instruction set as the file being dexopted.
  const std::vector<const DexFile*>& boot_class_path = class_linker->GetBootClassPath();
  for (size_t i = 0; i < boot_class_path.size(); i++) {
    if (boot_class_path[i]->GetLocation() == filename) {
      if (kVerboseLogging) {
        LOG(INFO) << "DexFile_isDexOptNeeded ignoring boot class path file: " << filename;
      }
      return JNI_FALSE;
    }
  }

  const InstructionSet target_instruction_set = GetInstructionSetFromString(instruction_set);

  // Check if we have an odex file next to the dex file.
  std::string odex_filename(DexFilenameToOdexFilename(filename, kRuntimeISA));
  std::string error_msg;
  std::unique_ptr<const OatFile> oat_file(OatFile::Open(odex_filename, odex_filename, NULL, false,
                                                        &error_msg));
  if (oat_file.get() == nullptr) {
    if (kVerboseLogging) {
      LOG(INFO) << "DexFile_isDexOptNeeded failed to open oat file '" << filename
          << "': " << error_msg;
    }
    error_msg.clear();
  } else {
    const art::OatFile::OatDexFile* oat_dex_file = oat_file->GetOatDexFile(filename, NULL,
                                                                           kReasonLogging);
    if (oat_dex_file != nullptr) {
      uint32_t location_checksum;
      // If its not possible to read the classes.dex assume up-to-date as we won't be able to
      // compile it anyway.
      if (!DexFile::GetChecksum(filename, &location_checksum, &error_msg)) {
        if (kVerboseLogging) {
          LOG(INFO) << "DexFile_isDexOptNeeded ignoring precompiled stripped file: "
              << filename << ": " << error_msg;
        }
        return JNI_FALSE;
      }
      if (ClassLinker::VerifyOatFileChecksums(oat_file.get(), filename, location_checksum,
                                              target_instruction_set,
                                              &error_msg)) {
        if (kVerboseLogging) {
          LOG(INFO) << "DexFile_isDexOptNeeded precompiled file " << odex_filename
              << " has an up-to-date checksum compared to " << filename;
        }
        return JNI_FALSE;
      } else {
        if (kVerboseLogging) {
          LOG(INFO) << "DexFile_isDexOptNeeded found precompiled file " << odex_filename
              << " with an out-of-date checksum compared to " << filename
              << ": " << error_msg;
        }
        error_msg.clear();
      }
    }
  }

  // Check the profile file.  We need to rerun dex2oat if the profile has changed significantly
  // since the last time, or it's new.
  // If the 'defer' argument is true then this will be retried later.  In this case we
  // need to make sure that the profile file copy is not made so that we will get the
  // same result second time.
  if (Runtime::Current()->GetProfilerOptions().IsEnabled() && (pkgname != nullptr)) {
    const std::string profile_file = GetDalvikCacheOrDie("profiles", false /* create_if_absent */)
        + std::string("/") + pkgname;
    const std::string prev_profile_file = profile_file + std::string("@old");

    struct stat profstat, prevstat;
    int e1 = stat(profile_file.c_str(), &profstat);
    int e2 = stat(prev_profile_file.c_str(), &prevstat);
    if (e1 < 0) {
      // No profile file, need to run dex2oat
      if (kReasonLogging) {
        LOG(INFO) << "DexFile_isDexOptNeeded profile file " << profile_file << " doesn't exist";
      }
      return JNI_TRUE;
    }

    if (e2 == 0) {
      // There is a previous profile file.  Check if the profile has changed significantly.
      // A change in profile is considered significant if X% (change_thr property) of the top K%
      // (compile_thr property) samples has changed.
      double top_k_threshold = Runtime::Current()->GetProfilerOptions().GetTopKThreshold();
      double change_threshold = Runtime::Current()->GetProfilerOptions().GetTopKChangeThreshold();
      double change_percent = 0.0;
      ProfileFile new_profile, old_profile;
      bool new_ok = new_profile.LoadFile(profile_file);
      bool old_ok = old_profile.LoadFile(prev_profile_file);
      if (!new_ok || !old_ok) {
        if (kVerboseLogging) {
          LOG(INFO) << "DexFile_isDexOptNeeded Ignoring invalid profiles: "
                    << (new_ok ?  "" : profile_file) << " " << (old_ok ? "" : prev_profile_file);
        }
      } else {
        std::set<std::string> new_top_k, old_top_k;
        new_profile.GetTopKSamples(new_top_k, top_k_threshold);
        old_profile.GetTopKSamples(old_top_k, top_k_threshold);
        if (new_top_k.empty()) {
          if (kVerboseLogging) {
            LOG(INFO) << "DexFile_isDexOptNeeded empty profile: " << profile_file;
          }
          // If the new topK is empty we shouldn't optimize so we leave the change_percent at 0.0.
        } else {
          std::set<std::string> diff;
          std::set_difference(new_top_k.begin(), new_top_k.end(), old_top_k.begin(), old_top_k.end(),
            std::inserter(diff, diff.end()));
          // TODO: consider using the usedPercentage instead of the plain diff count.
          change_percent = 100.0 * static_cast<double>(diff.size()) / static_cast<double>(new_top_k.size());
          if (kVerboseLogging) {
            std::set<std::string>::iterator end = diff.end();
            for (std::set<std::string>::iterator it = diff.begin(); it != end; it++) {
              LOG(INFO) << "DexFile_isDexOptNeeded new in topK: " << *it;
            }
          }
        }
      }

      if (change_percent > change_threshold) {
        if (kReasonLogging) {
          LOG(INFO) << "DexFile_isDexOptNeeded size of new profile file " << profile_file <<
          " is significantly different from old profile file " << prev_profile_file << " (top "
          << top_k_threshold << "% samples changed in proportion of " << change_percent << "%)";
        }
        if (!defer) {
          CopyProfileFile(profile_file.c_str(), prev_profile_file.c_str());
        }
        return JNI_TRUE;
      }
    } else {
      // Previous profile does not exist.  Make a copy of the current one.
      if (kVerboseLogging) {
        LOG(INFO) << "DexFile_isDexOptNeeded previous profile doesn't exist: " << prev_profile_file;
      }
      if (!defer) {
        CopyProfileFile(profile_file.c_str(), prev_profile_file.c_str());
      }
    }
  }

  // Check if we have an oat file in the cache
  const std::string cache_dir(GetDalvikCacheOrDie(instruction_set));
  const std::string cache_location(
      GetDalvikCacheFilenameOrDie(filename, cache_dir.c_str()));
  oat_file.reset(OatFile::Open(cache_location, filename, NULL, false, &error_msg));
  if (oat_file.get() == nullptr) {
    if (kReasonLogging) {
      LOG(INFO) << "DexFile_isDexOptNeeded cache file " << cache_location
          << " does not exist for " << filename << ": " << error_msg;
    }
    return JNI_TRUE;
  }

  uint32_t location_checksum;
  if (!DexFile::GetChecksum(filename, &location_checksum, &error_msg)) {
    if (kReasonLogging) {
      LOG(ERROR) << "DexFile_isDexOptNeeded failed to compute checksum of " << filename
            << " (error " << error_msg << ")";
    }
    return JNI_TRUE;
  }

  if (!ClassLinker::VerifyOatFileChecksums(oat_file.get(), filename, location_checksum,
                                           target_instruction_set, &error_msg)) {
    if (kReasonLogging) {
      LOG(INFO) << "DexFile_isDexOptNeeded cache file " << cache_location
          << " has out-of-date checksum compared to " << filename
          << " (error " << error_msg << ")";
    }
    return JNI_TRUE;
  }

  if (kVerboseLogging) {
    LOG(INFO) << "DexFile_isDexOptNeeded cache file " << cache_location
              << " is up-to-date for " << filename;
  }
  CHECK(error_msg.empty()) << error_msg;
  return JNI_FALSE;
}

static jboolean DexFile_isDexOptNeededInternal(JNIEnv* env, jclass, jstring javaFilename,
    jstring javaPkgname, jstring javaInstructionSet, jboolean defer) {
  ScopedUtfChars filename(env, javaFilename);
  NullableScopedUtfChars pkgname(env, javaPkgname);
  ScopedUtfChars instruction_set(env, javaInstructionSet);

  return IsDexOptNeededInternal(env, filename.c_str(), pkgname.c_str(),
                                instruction_set.c_str(), defer);
}

// public API, NULL pkgname
static jboolean DexFile_isDexOptNeeded(JNIEnv* env, jclass, jstring javaFilename) {
  const char* instruction_set = GetInstructionSetString(kRuntimeISA);
  ScopedUtfChars filename(env, javaFilename);
  return IsDexOptNeededInternal(env, filename.c_str(), nullptr /* pkgname */,
                                instruction_set, false /* defer */);
}


static JNINativeMethod gMethods[] = {
  NATIVE_METHOD(DexFile, closeDexFile, "(J)V"),
  NATIVE_METHOD(DexFile, defineClassNative, "(Ljava/lang/String;Ljava/lang/ClassLoader;J)Ljava/lang/Class;"),
  NATIVE_METHOD(DexFile, getClassNameList, "(J)[Ljava/lang/String;"),
  NATIVE_METHOD(DexFile, isDexOptNeeded, "(Ljava/lang/String;)Z"),
  NATIVE_METHOD(DexFile, isDexOptNeededInternal, "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Z)Z"),
  NATIVE_METHOD(DexFile, openDexFileNative, "(Ljava/lang/String;Ljava/lang/String;I)J"),
};

void register_dalvik_system_DexFile(JNIEnv* env) {
  REGISTER_NATIVE_METHODS("dalvik/system/DexFile");
}

}  // namespace art
