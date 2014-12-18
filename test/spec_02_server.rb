require 'rack'
require File.expand_path('../testrequest', __FILE__)
require 'timeout'
require 'rack/handler/rhebok'

describe Rhebok do
  extend TestRequest::Helpers
  begin

    @host = '127.0.0.1'
    @port = 9202
    @app = Rack::Lint.new(TestRequest.new)
    @pid = fork
    if @pid == nil
      #child
      Rack::Handler::Rhebok.run(@app, :Host=>'127.0.0.1', :Port=>9202, :MaxWorkers=>1)
      exit!(true)
    end
    sleep 1

  # test
  should "respond" do
    GET("/")
    response.should.not.be.nil
  end

  should "be a Rhebok" do
    GET("/")
    status.should.equal 200
    response["SERVER_PROTOCOL"].should.equal "HTTP/1.1"
    response["SERVER_PORT"].should.equal "9202"
    response["SERVER_NAME"].should.equal "127.0.0.1"
  end

  should "have rack headers" do
    GET("/")
    response["rack.version"].should.equal [1,1]
    response["rack.multithread"].should.equal false
    response["rack.multiprocess"].should.equal true
    response["rack.run_once"].should.equal false
  end

  should "multiple header" do
    GET("/")
    header["Content-Type"].should.equal "text/yaml"
    header["X-Foo"].should.equal "Foo, Bar"
    header["X-Bar"].should.equal "Foo, Bar"
    header["X-Baz"].should.equal "Baz"
    header["X-Fuga"].should.equal "Fuga"
  end


  should "have CGI headers on GET" do
    GET("/")
    response["REQUEST_METHOD"].should.equal "GET"
    response["PATH_INFO"].should.be.equal "/"
    response["QUERY_STRING"].should.equal ""
    response["test.postdata"].should.equal ""

    GET("/test/foo?quux=1")
    response["REQUEST_METHOD"].should.equal "GET"
    response["PATH_INFO"].should.equal "/test/foo"
    response["QUERY_STRING"].should.equal "quux=1"
  end

  should "have CGI headers on POST" do
    POST("/", {"rack-form-data" => "23"}, {'X-test-header' => '42'})
    status.should.equal 200
    response["REQUEST_METHOD"].should.equal "POST"
    response["QUERY_STRING"].should.equal ""
    response["HTTP_X_TEST_HEADER"].should.equal "42"
    response["test.postdata"].should.equal "rack-form-data=23"
  end

  should "support HTTP auth" do
    GET("/test", {:user => "ruth", :passwd => "secret"})
    response["HTTP_AUTHORIZATION"].should.equal "Basic cnV0aDpzZWNyZXQ="
  end

  should "set status" do
    GET("/test?secret")
    status.should.equal 403
    response["rack.url_scheme"].should.equal "http"
  end


  ensure
   sleep 1
   if @pid != nil
     Process.kill(:TERM, @pid)
     Process.wait()
   end
  end

end


