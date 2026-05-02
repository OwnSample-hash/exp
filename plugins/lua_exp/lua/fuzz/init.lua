return {
  name = "fuzz",
  version = "0.1.0",
  description = "A simple fuzzing tool",
  tags = { "fuzz", "testing" },
  vars = {
    target = "http://localhost:8080",
    fuzz_key = "FUZZ",
    dict = "default.dict",
    kind = "GET",
    header = "User-Agent: fuzz-tool||Content-Type: application/json",
    data = "",
    wrong_status_code = 500,
    wrong_response_size = 1000,
    wrong_response_time = 1000,
    threshold = 1,
    thread_delay = 100,
    threads = 4,
  },
  initialize = function()
    explo.logi("Initializing fuzz tool")
  end,
  shutdown = function()
    explo.logi("Shutting down fuzz tool")
  end,
  execute = function() end,
}

-- Vim: set expandtab tabstop=2 shiftwidth=2:
