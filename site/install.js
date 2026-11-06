// Install page: the model picker switches the installer's manifest; the release check shows the version or
// "No release yet"; browsers without Web Serial get a message instead of the button.
//
// Where the firmware comes from: GitHub's release downloads send no Access-Control-Allow-Origin header, so ESP
// Web Tools (which fetches the image from this page) can't read them. The Pages workflow therefore copies the
// latest release's install images into firmware/ beside this page and writes firmware/release.json
// ({"tag": "v0.2.0", ...}); the manifests name the images by relative path. Should GitHub ever allow
// cross-origin reads, point the manifests' "path" at
// https://github.com/ChristopherJacob/contrail/releases/latest/download/<file> and drop the copy step.
const RELEASE_INFO = "firmware/release.json";

const button = document.getElementById("install-button");
const installer = document.getElementById("installer");
const status = document.getElementById("release-status");
const noRelease = document.getElementById("no-release");
const noSerial = document.getElementById("no-serial");

for (const radio of document.querySelectorAll('input[name="model"]')) {
    radio.addEventListener("change", () => {
        if (radio.checked) button.setAttribute("manifest", `manifest-${radio.value}.json`);
    });
    if (radio.checked) button.setAttribute("manifest", `manifest-${radio.value}.json`);
}

async function latestRelease() {
    try {
        const r = await fetch(RELEASE_INFO, { cache: "no-store" });
        if (!r.ok) return null;
        const info = await r.json();
        return typeof info.tag === "string" && info.tag ? info : null;
    } catch {
        return null;
    }
}

const release = await latestRelease();
if (!release) {
    status.hidden = true;
    noRelease.hidden = false;
} else {
    status.classList.add("ok");
    status.replaceChildren("Latest version: ", Object.assign(document.createElement("b"), { textContent: release.tag }));
    if ("serial" in navigator) installer.hidden = false;
    else noSerial.hidden = false;
}
