// dng_to_tiff — batch DNG → 16-bit TIFF using the Adobe DNG SDK.
//
// Usage:
//   dng_to_tiff [-cs <space>] [-out <dir>] [-ev <stops>] [-shadows <n>]
//               [-dcp <profile.dcp>] file1.dng [file2.dng ...]
//
// Color space options (default: sRGB):
//   srgb       sRGB (default; best for Zerene Stacker input)
//   adobe      Adobe RGB
//   prophoto   ProPhoto RGB
//   p3         Display P3
//
// The output TIFF name matches the input stem with a .tif extension.
// If -out is not given, TIFFs are written beside the input files.
// An existing output is replaced only after the new file is completely written.

#include "dng_camera_profile.h"
#include "dng_color_space.h"
#include "dng_exceptions.h"
#include "dng_file_stream.h"
#include "dng_host.h"
#include "dng_ifd.h"
#include "dng_image_writer.h"
#include "dng_info.h"
#include "dng_negative.h"
#include "dng_render.h"
#include "dng_tag_types.h"
#include "dng_tag_values.h"
#include "dng_xmp.h"
#include "dng_xmp_sdk.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------

static std::string stem(const std::string &path)
{
    // Remove directory prefix.
    const size_t slash = path.find_last_of('/');
    std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);
    // Remove extension (but keep a leading dot of a hidden file).
    const size_t dot = name.find_last_of('.');
    if (dot != std::string::npos && dot > 0)
        name.resize(dot);
    return name;
}

static std::string dirOf(const std::string &path)
{
    const size_t slash = path.find_last_of('/');
    if (slash == std::string::npos) return ".";
    return path.substr(0, slash);
}

static std::string outputPathFor(const std::string &input, const std::string &outDir)
{
    std::string out = outDir.empty() ? dirOf(input) : outDir;
    out += '/';
    out += stem(input);
    out += ".tif";
    return out;
}

static bool isDirectory(const std::string &path)
{
    struct stat st {};
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

static bool sameFile(const std::string &a, const std::string &b)
{
    struct stat sa {};
    struct stat sb {};
    return stat(a.c_str(), &sa) == 0 && stat(b.c_str(), &sb) == 0 &&
           sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
}

// A key identifying an output path before the file exists. The name is folded
// to lower case because the default macOS file system ignores case.
static std::string outputKey(const std::string &output)
{
    char resolved[PATH_MAX];
    const std::string dir = dirOf(output);
    std::string key = (realpath(dir.c_str(), resolved) != nullptr) ? resolved : dir;
    std::string name = output.substr(output.find_last_of('/') + 1);
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    key += '/';
    key += name;
    return key;
}

// Refuse, before any work is done, a batch that would overwrite one of its own
// inputs or write two inputs to the same output.
static bool checkOutputs(const std::vector<std::string> &inputs, const std::string &outDir)
{
    std::set<std::string> seen;
    for (const std::string &input : inputs)
    {
        const std::string output = outputPathFor(input, outDir);
        for (const std::string &other : inputs)
        {
            if (sameFile(output, other))
            {
                fprintf(stderr, "Output %s would overwrite input %s\n",
                        output.c_str(), other.c_str());
                return false;
            }
        }
        if (!seen.insert(outputKey(output)).second)
        {
            fprintf(stderr, "More than one input would be written to %s\n", output.c_str());
            return false;
        }
    }
    return true;
}

static bool parseNumber(const char *text, double &value)
{
    char *end = nullptr;
    const double parsed = strtod(text, &end);
    if (end == text || *end != '\0' || !std::isfinite(parsed))
        return false;
    value = parsed;
    return true;
}

static std::string errnoText()
{
    char buffer[256] = "unknown error";
    (void) strerror_r(errno, buffer, sizeof buffer);
    return buffer;
}

namespace
{

// Removes the temporary output unless it was renamed into place.
class TempFileGuard
{
public:
    explicit TempFileGuard(std::string path) : fPath(std::move(path)) {}
    TempFileGuard(const TempFileGuard &) = delete;
    TempFileGuard &operator=(const TempFileGuard &) = delete;
    ~TempFileGuard()
    {
        if (!fCommitted) unlink(fPath.c_str());
    }
    void commit() { fCommitted = true; }

private:
    std::string fPath;
    bool fCommitted = false;
};

// Keeps the XMP SDK initialized for the lifetime of the batch.
class XmpSdkScope
{
public:
    XmpSdkScope() { dng_xmp_sdk::InitializeSDK(); }
    XmpSdkScope(const XmpSdkScope &) = delete;
    XmpSdkScope &operator=(const XmpSdkScope &) = delete;
    ~XmpSdkScope() { dng_xmp_sdk::TerminateSDK(); }
};

}  // namespace

// ---------------------------------------------------------------------------

static bool convertOne(const char *inputPath,
                       const char *outputPath,
                       const dng_color_space &colorSpace,
                       double exposureEV,
                       double shadows,
                       const char *dcpPath)
{
    fprintf(stderr, "Converting %s ...\n", inputPath);

    try
    {
        dng_host host;

        dng_file_stream stream(inputPath);

        dng_info info;
        info.Parse(host, stream);
        info.PostParse(host);

        if (!info.IsValidDNG())
        {
            fprintf(stderr, "  ERROR: not a valid DNG: %s\n", inputPath);
            return false;
        }

        AutoPtr<dng_negative> negative;
        negative.Reset(host.Make_dng_negative());

        negative->Parse(host, stream, info);
        negative->PostParse(host, stream, info);

        // Optional external camera profile (.dcp), e.g. Adobe Standard.
        dng_camera_profile_id dcpID;
        if (dcpPath != nullptr)
        {
            AutoPtr<dng_camera_profile> prof(new dng_camera_profile);
            dng_file_stream dcpStream(dcpPath);
            if (!prof->ParseExtended(dcpStream) || !prof->IsValid(negative->ColorChannels()))
            {
                fprintf(stderr, "  ERROR: cannot use profile: %s\n", dcpPath);
                return false;
            }
            // AddProfile renames a nameless profile, which changes its ID, so
            // name it first and capture the ID afterward.
            if (prof->Name().IsEmpty())
                prof->SetName("External DCP");
            dcpID = prof->ProfileID();
            negative->AddProfile(prof);

            dng_camera_profile added;
            if (!negative->GetProfileByID(dcpID, added, /*useDefaultIfNoMatch=*/false))
            {
                fprintf(stderr, "  ERROR: cannot select profile: %s\n", dcpPath);
                return false;
            }
        }

        // Read the raw (stage 1) image. An enhanced image, if the DNG has one,
        // is deliberately ignored: this tool always renders from the raw data.
        {
            const dng_timer t("Read stage 1");
            negative->ReadStage1Image(host, stream, info);
        }

        if (info.fMaskIndex != -1)
        {
            const dng_timer t("Read transparency mask");
            negative->ReadTransparencyMask(host, stream, info);
        }

        negative->SynchronizeMetadata();

        // Linearise raw → stage 2 (apply black subtraction, linearisation table).
        {
            const dng_timer t("Linearise");
            negative->BuildStage2Image(host);
        }

        // Demosaic → stage 3 (full-colour, full-resolution).
        {
            const dng_timer t("Demosaic");
            negative->BuildStage3Image(host, /*srcPlane=*/-1);
        }

        // Render: apply colour matrix, white balance, tone curve.
        dng_render render(host, *negative);
        render.SetFinalSpace(colorSpace);
        render.SetFinalPixelType(ttShort);  // 16-bit
        if (dcpPath != nullptr) render.SetCameraProfileID(dcpID);
        if (exposureEV != 0.0) render.SetExposure(exposureEV);
        render.SetShadows(shadows);

        AutoPtr<dng_image> finalImage;
        {
            const dng_timer t("Render");
            finalImage.Reset(render.Render());
        }

        // Apply EXIF orientation so the TIFF pixel order is upright.
        finalImage->Rotate(negative->Orientation());

        // These settings describe a raw conversion that is now baked in. Left
        // in the TIFF, Adobe apps could apply them again to rendered pixels.
        if (negative->GetXMP() != nullptr)
        {
            negative->GetXMP()->RemoveProperties(XMP_NS_CRS);
            negative->GetXMP()->RemoveProperties(XMP_NS_CRSS);
            negative->GetXMP()->RemoveProperties(XMP_NS_CRD);
            negative->GetXMP()->RemoveProperties(XMP_NS_CRLCP);
        }

        // Write to a temporary file in the same directory, then rename, so a
        // failure can never leave a partial TIFF or destroy an earlier good one.
        const std::string tmpPath =
            std::string(outputPath) + ".tmp." + std::to_string(getpid());
        const int fd = open(tmpPath.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
        if (fd < 0)
        {
            fprintf(stderr, "  ERROR: cannot create %s: %s\n", tmpPath.c_str(), errnoText().c_str());
            return false;
        }
        TempFileGuard guard(tmpPath);
        FILE *file = fdopen(fd, "wb");
        if (file == nullptr)
        {
            fprintf(stderr, "  ERROR: cannot open %s: %s\n", tmpPath.c_str(), errnoText().c_str());
            close(fd);
            return false;
        }

        {
            dng_file_stream out(file);  // closes the FILE when destroyed

            const dng_camera_profile *profilePtr = nullptr;
            dng_camera_profile profile;
            if (!negative->IsMonochrome())
            {
                if (negative->GetProfileByID(render.CameraProfileID(), profile))
                    profilePtr = &profile;
            }

            dng_image_writer writer;
            writer.WriteTIFF(host,
                             out,
                             *finalImage.Get(),
                             finalImage->Planes() >= 3 ? piRGB : piBlackIsZero,
                             ccUncompressed,
                             &negative->Metadata(),
                             &render.FinalSpace(profilePtr));

            // The stream's destructor ignores close errors, so surface them here.
            out.Flush();
            if (fflush(file) != 0 || fsync(fileno(file)) != 0)
            {
                fprintf(stderr, "  ERROR: cannot write %s: %s\n", tmpPath.c_str(), errnoText().c_str());
                return false;
            }
        }

        if (rename(tmpPath.c_str(), outputPath) != 0)
        {
            fprintf(stderr, "  ERROR: cannot create %s: %s\n", outputPath, errnoText().c_str());
            return false;
        }
        guard.commit();

        fprintf(stderr, "  → %s\n", outputPath);
        return true;
    }
    catch (const dng_exception &e)
    {
        fprintf(stderr, "  ERROR (DNG %d): %s\n", e.ErrorCode(), inputPath);
        return false;
    }
    catch (...)
    {
        fprintf(stderr, "  ERROR (unknown): %s\n", inputPath);
        return false;
    }
}

// ---------------------------------------------------------------------------

static void usage(const char *argv0)
{
    fprintf(stderr,
            "Usage: %s [-cs srgb|adobe|prophoto|p3] [-out <dir>] [-ev <stops>] [-shadows <n>] [-dcp <profile.dcp>] file.dng ...\n",
            argv0);
}

static int run(int argc, char *argv[])
{
    if (argc < 2)
    {
        usage(argv[0]);
        return 1;
    }

    const dng_color_space *colorSpace = &dng_space_sRGB::Get();
    std::string outDir;
    double exposureEV = 0.0;
    const char *dcpPath = nullptr;
    double shadows    = 0.0;   // SDK default is 5.0, which crushes shadows on profile-less DNGs
    std::vector<std::string> inputs;

    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "-cs") == 0 && i + 1 < argc)
        {
            ++i;
            if      (strcmp(argv[i], "adobe")    == 0) colorSpace = &dng_space_AdobeRGB::Get();
            else if (strcmp(argv[i], "prophoto") == 0) colorSpace = &dng_space_ProPhoto::Get();
            else if (strcmp(argv[i], "p3")       == 0) colorSpace = &dng_space_DisplayP3::Get();
            else if (strcmp(argv[i], "srgb")     == 0) colorSpace = &dng_space_sRGB::Get();
            else
            {
                fprintf(stderr, "Unknown color space: %s\n", argv[i]);
                usage(argv[0]);
                return 1;
            }
        }
        else if (strcmp(argv[i], "-ev") == 0 && i + 1 < argc)
        {
            ++i;
            if (!parseNumber(argv[i], exposureEV))
            {
                fprintf(stderr, "Invalid -ev value: %s\n", argv[i]);
                return 1;
            }
        }
        else if (strcmp(argv[i], "-dcp") == 0 && i + 1 < argc)
        {
            dcpPath = argv[++i];
        }
        else if (strcmp(argv[i], "-shadows") == 0 && i + 1 < argc)
        {
            ++i;
            if (!parseNumber(argv[i], shadows) || shadows < 0.0)
            {
                fprintf(stderr, "Invalid -shadows value: %s\n", argv[i]);
                return 1;
            }
        }
        else if (strcmp(argv[i], "-out") == 0 && i + 1 < argc)
        {
            outDir = argv[++i];
        }
        else if (argv[i][0] == '-')
        {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
        else
        {
            inputs.emplace_back(argv[i]);
        }
    }

    if (inputs.empty())
    {
        fprintf(stderr, "No input files.\n");
        usage(argv[0]);
        return 1;
    }

    if (!outDir.empty() && !isDirectory(outDir))
    {
        fprintf(stderr, "Output directory does not exist: %s\n", outDir.c_str());
        return 1;
    }

    if (!checkOutputs(inputs, outDir))
        return 1;

    const XmpSdkScope xmpSdk;

    int failed = 0;
    for (const std::string &inp : inputs)
    {
        const std::string out = outputPathFor(inp, outDir);
        if (!convertOne(inp.c_str(), out.c_str(), *colorSpace, exposureEV, shadows, dcpPath))
            ++failed;
    }

    return failed != 0 ? 1 : 0;
}

int main(int argc, char *argv[])
{
    try
    {
        return run(argc, argv);
    }
    catch (...)
    {
        fputs("ERROR: unexpected failure\n", stderr);
        return 1;
    }
}
