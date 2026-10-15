-- a module for runtime_test (require "helper")
HELPER_LOADS = (HELPER_LOADS or 0) + 1
return { answer = 42, loads = HELPER_LOADS }
