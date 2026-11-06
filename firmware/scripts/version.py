# PlatformIO extra script: CONTRAIL_VERSION from the release tag, else `git describe`, else "dev".
# A post: script, so the define goes on the project's sources only (projenv): a new version (each commit, or
# --dirty flipping) recompiles main_*.cpp, not every library and the framework.
import os
import subprocess

Import("env", "projenv")  # noqa: F821 (PlatformIO's SCons globals)


def version():
    ref = os.environ.get("GITHUB_REF_NAME", "")
    if os.environ.get("GITHUB_REF_TYPE") == "tag" and ref.startswith("v"):
        return ref[1:]
    try:
        out = subprocess.run(["git", "describe", "--tags", "--dirty", "--always"], capture_output=True,
                             text=True, cwd=env.subst("$PROJECT_DIR"), timeout=10)  # noqa: F821
        tag = out.stdout.strip()
        if out.returncode == 0 and tag and "." in tag:
            return tag.lstrip("v")
    except (OSError, subprocess.SubprocessError):
        pass
    return "dev"


projenv.Append(CPPDEFINES=[("CONTRAIL_VERSION", env.StringifyMacro(version()))])  # noqa: F821
