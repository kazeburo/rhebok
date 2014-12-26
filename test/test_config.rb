host '127.0.0.1'
port 9202
max_workers 1
before_fork {
  ENV["TEST_FOO"] = "BAZ"
}
after_fork {
  ENV["TEST_BAR"] = "BAR"
}
