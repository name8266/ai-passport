#include "worldcam_live_sources.h"

const wc_live_source_t wc_live_sources[] = {
    {
        "Sonntagberg",
        "Austria",
        "HILLS",
        "https://sonntagberg-cam.landsteiner.at/mjpg/video.mjpg",
        "https://www.sonntagberg.at/webcam-sonntagberg/"
    },
    {
        "Schwaebisch Hall",
        "Germany",
        "CITY",
        "https://webcam.schwaebischhall.de/mjpg/video.mjpg",
        "https://webcam.schwaebischhall.de/"
    },
    {
        "McKeldin East View",
        "USA",
        "CITY",
        "http://cam-mckeldin-eastview.umd.edu/axis-cgi/mjpg/video.cgi",
        "https://umd.edu/"
    },
    {
        "Strandafjellet Gondola",
        "Norway",
        "SNOW",
        "https://camera.strandafjellet.no:8447/mjpg/video.mjpg",
        "https://www.strandafjellet.no/en/webcamera"
    },
    {
        "Strandafjellet Roalden",
        "Norway",
        "SNOW",
        "https://camera.strandafjellet.no:8443/mjpg/video.mjpg",
        "https://www.strandafjellet.no/en/webcamera"
    },
    {
        "Strandafjellet Furset",
        "Norway",
        "SNOW",
        "https://camera.strandafjellet.no:8442/mjpg/video.mjpg",
        "https://www.strandafjellet.no/en/webcamera"
    },
    {
        "Les Issambres Port",
        "France",
        "COAST",
        "http://92.182.16.200:81/mjpg/video.mjpg?streamprofile=video",
        "https://port-des-issambres.fr/en/stopover/"
    },
    {
        "Bibione Beach",
        "Italy",
        "BEACH",
        "http://www.webcam.adriatur.it/axis-cgi/mjpg/video.cgi",
        "https://holidaylivecam.com/camera/1200/Bibione%2BBeach%2Bwebcam%2B-%2BBibione%2Bspiaggia%2Blive%2Bcam"
    }
};

const size_t wc_live_source_count =
    sizeof(wc_live_sources) / sizeof(wc_live_sources[0]);
