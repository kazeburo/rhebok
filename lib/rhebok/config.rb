class Rhebok
  class Config
    def initialize(options={})
      @config = options
    end

    def host(val)
      @config[:Host] = val
    end

    def port(val)
      @config[:Port] = val
    end

    def path(val)
      @config[:Path] = val
    end

    def max_workers(val)
      @config[:MaxWorkers] = val
    end

    def timeout(val)
      @config[:Timeout] = val
    end

    def max_request_per_child(val)
      @config[:MaxRequestPerChild] = val
    end

    def min_request_per_child(val)
      @config[:MinRequestPerChild] = val
    end

    def spawn_interval(val)
      @config[:SpawnInterval] = val
    end

    def err_respawn_interval(val)
      @config[:ErrRespawnInterval] = val
    end

    def oobgc(val)
      @config[:OobGC] = val
    end

    def max_gc_per_request(val)
      @config[:MaxGCPerRequest] = val
    end

    def min_gc_per_request(val)
      @config[:MinGCPerRequest] = val
    end

    def backlog(val)
      @config[:BackLog] = val
    end

    def before_fork(&block)
      @config[:BeforeFork] = block
    end

    def after_fork(&block)
      @config[:AfterFork] = block
    end

    def retrieve
      @config
    end
  end
end
