// dng_to_tiff — batch DNG → 16-bit sRGB TIFF using the Adobe DNG SDK.
//
// Usage:
//   dng_to_tiff [-cs <space>] [-out <dir>] file1.dng [file2.dng ...]
//
// Color space options (default: sRGB):
//   srgb       sRGB (default; best for Zerene Stacker input)
//   adobe      Adobe RGB
//   prophoto   ProPhoto RGB
//   p3         Display P3
//
// The output TIFF name matches the input stem with a .tif extension.
// If -out is not given, TIFFs are written beside the input files.

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
#include "dng_xmp_sdk.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------

static std::string stem(const std::string &path)
{
    // Remove directory prefix.
    size_t slash = path.find_last_of("/\\");
    std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);
    // Remove extension.
    size_t dot = name.find_last_of('.');
    if (dot != std::string::npos)
        name = name.substr(0, dot);
    return name;
}

static std::string dirOf(const std::string &path)
{
    size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos) return ".";
    return path.substr(0, slash);
}

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
        if (dcpPath)
        {
            AutoPtr<dng_camera_profile> prof(new dng_camera_profile);
            dng_file_stream dcpStream(dcpPath);
            if (!prof->ParseExtended(dcpStream))
            {
                fprintf(stderr, "  ERROR: cannot parse profile: %s\n", dcpPath);
                return false;
            }
            dcpID = prof->ProfileID();
            negative->AddProfile(prof);
        }

        // Read the raw (stage 1) image.  For dnglab-wrapped Sony ARW the
        // enhanced IFD does not exist, so ReadStage1Image is always correct.
        {
            dng_timer t("Read stage 1");
            negative->ReadStage1Image(host, stream, info);
        }

        negative->SynchronizeMetadata();

        // Linearise raw → stage 2 (apply black subtraction, linearisation table).
        {
            dng_timer t("Linearise");
            negative->BuildStage2Image(host);
        }

        // Demosaic → stage 3 (full-colour, full-resolution).
        {
            dng_timer t("Demosaic");
            negative->BuildStage3Image(host, /*mosaicPlane=*/-1);
        }

        // Render: apply colour matrix, white balance, tone curve.
        dng_render render(host, *negative);
        render.SetFinalSpace(colorSpace);
        render.SetFinalPixelType(ttShort);  // 16-bit
        if (dcpPath) render.SetCameraProfileID(dcpID);
        if (exposureEV != 0.0) render.SetExposure(exposureEV);
        render.SetShadows(shadows);

        AutoPtr<dng_image> finalImage;
        {
            dng_timer t("Render");
            finalImage.Reset(render.Render());
        }

        // Apply EXIF orientation so the TIFF pixel order is upright.
        finalImage->Rotate(negative->Orientation());

        // Write TIFF.
        {
            dng_file_stream out(outputPath, /*forWrite=*/true);

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
        }

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

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        usage(argv[0]);
        return 1;
    }

    // Initialise XMP before any DNG work.
    dng_xmp_sdk::InitializeSDK();

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
            exposureEV = atof(argv[++i]);
        else if (strcmp(argv[i], "-dcp") == 0 && i + 1 < argc)
            dcpPath = argv[++i];
        else if (strcmp(argv[i], "-shadows") == 0 && i + 1 < argc)
            shadows = atof(argv[++i]);
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
            inputs.push_back(argv[i]);
        }
    }

    if (inputs.empty())
    {
        fprintf(stderr, "No input files.\n");
        usage(argv[0]);
        return 1;
    }

    int failed = 0;
    for (const std::string &inp : inputs)
    {
        std::string dir  = outDir.empty() ? dirOf(inp) : outDir;
        std::string name = stem(inp) + ".tif";
        std::string out  = dir + "/" + name;

        if (!convertOne(inp.c_str(), out.c_str(), *colorSpace, exposureEV, shadows, dcpPath))
            ++failed;
    }

    dng_xmp_sdk::TerminateSDK();

    return failed ? 1 : 0;
}
