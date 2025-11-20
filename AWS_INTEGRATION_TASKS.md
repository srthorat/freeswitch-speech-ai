# AWS SDK Integration - Detailed Task List

## Investigation Complete ✓

### Key Findings:
1. **AWS SDK is already being built** in Dockerfile (lines 105-116) using version 1.11.345
2. **mod_aws_transcribe Makefile has incorrect paths** - points to source directory instead of installed location
3. **No AWS SDK detection in configure.ac.extra** - the `--with-aws=yes` flag does nothing
4. **Missing dependency**: libpulse-dev not in Dockerfile.freeswitch-base

---

## Tasks to Complete

### Task 1: Fix mod_aws_transcribe Makefile.am ⚠️ CRITICAL
**Priority**: HIGH
**File**: `modules/mod_aws_transcribe/Makefile.am`

**Current Code (WRONG)**:
```makefile
mod_aws_transcribe_la_CXXFLAGS = $(AM_CXXFLAGS) -std=c++11 -I${switch_srcdir}/libs/aws-sdk-cpp/aws-cpp-sdk-core/include -I${switch_srcdir}/libs/aws-sdk-cpp/aws-cpp-sdk-transcribestreaming/include -I${switch_srcdir}/libs/aws-sdk-cpp/build/.deps/install/include

mod_aws_transcribe_la_LDFLAGS  = -avoid-version -module -no-undefined -L${switch_srcdir}/libs/aws-sdk-cpp/build/.deps/install/lib -L${switch_srcdir}/libs/aws-sdk-cpp/build/aws-cpp-sdk-core -L${switch_srcdir}/libs/aws-sdk-cpp/build/aws-cpp-sdk-transcribestreaming -laws-cpp-sdk-transcribestreaming -laws-cpp-sdk-core -laws-c-event-stream -laws-checksums -laws-c-common -lpthread -lcurl -lcrypto -lssl -lz
```

**Proposed Fix (Option A - Use pkg-config like mod_google_transcribe)**:
```makefile
mod_aws_transcribe_la_CXXFLAGS = $(AM_CXXFLAGS) -std=c++11 `pkg-config --cflags aws-cpp-sdk-transcribestreaming aws-cpp-sdk-core`

mod_aws_transcribe_la_LDFLAGS  = -avoid-version -module -no-undefined `pkg-config --libs aws-cpp-sdk-transcribestreaming aws-cpp-sdk-core` -laws-c-event-stream -laws-checksums -laws-c-common -lpthread
```

**Proposed Fix (Option B - Use system paths like mod_azure_transcribe)**:
```makefile
mod_aws_transcribe_la_CXXFLAGS = $(AM_CXXFLAGS) -std=c++11 -I/usr/local/include

mod_aws_transcribe_la_LDFLAGS  = -avoid-version -module -no-undefined -L/usr/local/lib -laws-cpp-sdk-transcribestreaming -laws-cpp-sdk-core -laws-c-event-stream -laws-checksums -laws-c-common -lpthread -lcurl -lcrypto -lssl -lz
```

**Recommendation**: Use Option A (pkg-config) as it's more flexible and follows the pattern of mod_google_transcribe. The Dockerfile already copies .pc files (line 116).

---

### Task 2: Add AWS SDK Detection to configure.ac.extra (Optional but Recommended)
**Priority**: MEDIUM
**File**: `files/configure.ac.extra`
**Location**: After line 1674 (after --with-extra section)

**Purpose**: Make `--with-aws=yes` flag actually work

**Code to Add**:
```autoconf
dnl ---------------------------------------------------------------------------
dnl - AWS SDK C++
dnl ---------------------------------------------------------------------------

AC_ARG_WITH(aws,
   [AS_HELP_STRING([--with-aws],
     [enable support for AWS SDK (aws-cpp-sdk-core and aws-cpp-sdk-transcribestreaming)])],
   [with_aws="$withval"],
   [with_aws="no"])

if test "$with_aws" = "yes"; then
  PKG_CHECK_MODULES([AWS_SDK], [aws-cpp-sdk-core aws-cpp-sdk-transcribestreaming], [
     AM_CONDITIONAL([HAVE_AWS_SDK],[true])
     AC_DEFINE([HAVE_AWS_SDK], [1], [Define to 1 if you have AWS SDK C++])], [
     AC_MSG_RESULT([no]); AM_CONDITIONAL([HAVE_AWS_SDK],[false])])
else
     AM_CONDITIONAL([HAVE_AWS_SDK],[false])
fi

AC_SUBST(AWS_SDK_LIBS)
AC_SUBST(AWS_SDK_CFLAGS)
```

**Note**: This is optional because the module can build without configure detection if we use fixed paths, but it's better practice to have proper detection.

---

### Task 3: Update Makefile.am.extra to Use Detected AWS SDK (Optional)
**Priority**: MEDIUM
**File**: `files/Makefile.am.extra`
**Location**: Line 449

**Current Code**:
```makefile
libfreeswitch_la_LIBADD  = $(CORE_LIBS) $(APR_LIBS) $(SQLITE_LIBS) $(GUMBO_LIBS) $(FVAD_LIBS) $(FREETYPE_LIBS) $(CURL_LIBS) $(PCRE_LIBS) $(SPEEX_LIBS) $(LIBEDIT_LIBS) $(SYSTEMD_LIBS) $(GRPC_LIBS) $(AWS_SDK_LIBS) $(openssl_LIBS) $(PLATFORM_CORE_LIBS) $(TPL_LIBS) $(SPANDSP_LIBS) $(SOFIA_SIP_LIBS)
```

**Change**: Already has $(AWS_SDK_LIBS) - this will work once Task 2 is completed.

---

### Task 4: Add libpulse-dev Dependency to Dockerfile
**Priority**: LOW
**File**: `dockerfiles/Dockerfile.freeswitch-base` OR `Dockerfile`
**Location**: Line 32 in Dockerfile (apt-get install section)

**Current**: Missing libpulse-dev

**Change**: Add `libpulse-dev` to the apt-get install list:
```dockerfile
    && apt-get install -y --quiet --no-install-recommends \
    python-is-python3 lsof gcc g++ make build-essential git autoconf automake default-mysql-client redis-tools \
    curl telnet libtool libtool-bin libssl-dev libcurl4-openssl-dev libz-dev liblz4-tool \
    libpulse-dev \
```

**Justification**: mod_aws_transcribe/README.md:32 lists libpulse-dev as a dependency for AWS SDK.

**Status**: Currently builds without it, so this is LOW priority. May be needed for certain AWS SDK features.

---

### Task 5: Update AWS SDK Version (Optional)
**Priority**: LOW
**File**: `.env` file or build scripts

**Current Version**: 1.11.345 (from README.md:428)
**Latest Available**: 1.11.619+
**Verified Working**: 1.11.200 (per mod_aws_transcribe/README.md:48)

**Recommendation**:
- Keep 1.11.345 for now (if it works)
- OR update to 1.11.400+ for latest features and bug fixes
- Test thoroughly after any version update

**Where to Change**:
- README.md line 428: `awsSdkCppVersion=1.11.345`
- This value is read by build-locally.sh and other build scripts

---

### Task 6: Verify mod_aws_transcribe Compiles
**Priority**: HIGH
**Actions**:
1. Apply Task 1 fixes
2. Rebuild Docker image or run local build
3. Check that mod_aws_transcribe.so is created
4. Verify with `ldd /usr/local/freeswitch/mod/mod_aws_transcribe.so`

**Validation**: The Dockerfile already has validation at lines 184-227 that checks for mod_aws_transcribe.so and its dependencies.

---

### Task 7: Test mod_aws_transcribe Functionality
**Priority**: HIGH
**Prerequisites**: Tasks 1 and 6 completed

**Test Steps**:
1. Start FreeSWITCH with the new build
2. Load module: `load mod_aws_transcribe`
3. Check for errors in freeswitch.log
4. Set up test AWS credentials
5. Test transcription command: `aws_transcribe <uuid> start en-US interim`
6. Verify transcription events are received

**Reference**: See modules/mod_aws_transcribe/README.md for full usage examples

---

## Summary of Changes Required

### Minimal Changes (MUST DO):
1. ✅ **Task 1**: Fix mod_aws_transcribe/Makefile.am paths
2. ✅ **Task 6**: Verify build succeeds

### Recommended Changes (SHOULD DO):
3. ✅ **Task 2**: Add AWS SDK detection to configure.ac.extra
4. ✅ **Task 7**: Test module functionality

### Optional Changes (NICE TO HAVE):
5. ⭕ **Task 4**: Add libpulse-dev dependency
6. ⭕ **Task 5**: Update AWS SDK version

---

## Files That Need Changes

| File | Task | Change Type | Priority |
|------|------|-------------|----------|
| `modules/mod_aws_transcribe/Makefile.am` | 1 | Fix paths | HIGH |
| `files/configure.ac.extra` | 2 | Add AWS detection | MEDIUM |
| `Dockerfile` | 4 | Add libpulse-dev | LOW |
| `README.md` | 5 | Update AWS version | LOW |

---

## Build Order

1. **First**: Fix Makefile.am (Task 1)
2. **Second**: Add configure.ac.extra detection (Task 2) - optional but recommended
3. **Third**: Rebuild and verify (Task 6)
4. **Fourth**: Test functionality (Task 7)
5. **Later**: Add libpulse-dev and update AWS version if needed (Tasks 4 & 5)

---

## Key Insights

### Why --with-aws=yes Doesn't Break the Build:
The configure script ignores unknown flags, so `--with-aws=yes` in Dockerfile:175 is silently ignored. This is why it doesn't cause an error.

### Why mod_aws_transcribe Might Currently Fail to Build:
The Makefile.am paths like `${switch_srcdir}/libs/aws-sdk-cpp/` don't exist because:
1. AWS SDK source is cloned in a separate Docker stage (aws-sdk-cpp)
2. Only compiled libraries are copied to the freeswitch stage (Dockerfile:131-132)
3. The source directory is never copied

### How Other Modules Handle This:
- **mod_azure_transcribe**: Uses `/usr/local/lib/MicrosoftSpeechSDK/x64` (system paths)
- **mod_google_transcribe**: Uses `pkg-config --libs grpc++` (pkg-config detection)
- **mod_aws_transcribe**: Should use one of these approaches instead of hardcoded source paths

---

## References

- AWS SDK C++ GitHub: https://github.com/aws/aws-sdk-cpp
- mod_aws_transcribe README: modules/mod_aws_transcribe/README.md
- FreeSWITCH configure.ac.extra: files/configure.ac.extra:1645-1674 (see --with-lws and --with-extra patterns)
- Reference repository: https://github.com/srthorat/freeswitch_modules/tree/main/conf (no AWS configuration found)

---

## Next Steps

1. Review this task list with the user
2. Get approval on which tasks to implement
3. Decide on Option A (pkg-config) vs Option B (fixed paths) for Task 1
4. Create a new branch and start implementation
5. Test build after each change
6. Commit and push when working

---

*Generated: 2025-11-20*
*Analysis based on: freeswitch-speech-ai repository commit 6f35db1*
