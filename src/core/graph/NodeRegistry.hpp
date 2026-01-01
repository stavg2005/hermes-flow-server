#pragma once

/**
 * @brief Bootstrapper for the Node Factory.
 * * @details
 * **Reflection-like Mechanism:**
 * C++ does not support reflection (creating classes from strings) natively.
 * This function manually registers mapping functions:
 * - "fileInput" -> `CreateFileInput()`
 * - "mixer"     -> `CreateMixer()`
 * * @note This must be called exactly once at application startup (in `main.cpp`).
 * If a node type is missing here, the parser will throw "Unknown node type"
 * at runtime.
 */
void RegisterBuiltinNodes();
