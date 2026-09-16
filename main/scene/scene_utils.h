    #ifndef SCENE_UTILS_H
    #define SCENE_UTILS_H

    #include <string>
    #include <cstdlib>

    // Conversione sicura da std::string a float (nessuna eccezione)
    static inline float safe_stof(const std::string &s) {
        if (s.empty()) return 0.0f;
        char *endptr;
        float val = strtof(s.c_str(), &endptr);
        if (endptr == s.c_str()) return 0.0f;
        return val;
    }

    #endif // SCENE_UTILS_H