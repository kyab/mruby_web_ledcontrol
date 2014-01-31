
class Request
	def initialize
		@request = ""
		@response = ""
		@count = 0
		@request_line = nil
		@method = nil
		@header_readed = false
		@header = {}

	end

	def add_request(buffer)
		puts "add_request comme on"
		@request += buffer
		if (@request_line.nil?)
			if @request =~ Regexp.new("(.*)\r\n", Regexp::MULTILINE)
				@request_line = Regexp.last_match[1]
			else
				@request += buffer
				return
			end
		end

		puts "point A"

		if (@method.nil?)
			case @request_line
			when /(GET) (.*) (.*)/
				@method = :GET
				@url = Regexp.last_match[2]
				@version = Regexp.last_match[3]
			when /(POST) (.*) (.*)/
				@method = :POST
				@url = Regexp.last_match[2]
				@version = Regexp.last_match[3]
			else
				puts "Unsupported HTTP Request?"
				puts @request_line
				return
			end
		end

		puts "method = #{@method}, url = #{@url}, version = #{@version}"

		if (!@header_readed)
			puts "-------dumping HTTP request---------"
			puts @request
			puts "----------------"
			if @request.end_with?("\r\n\r\n")
				@request.split("\r\n").each_with_index do |line, index|
					next if index == 0	#skip request line
					break if line == ""	#last line
					name,value = line.split(":")
					@header[name] = value
				end
				@header_readed = true
				puts "header read done"
			else
				puts "request shortage ?? still no response" #may return 400 Bad Request
			end
		end

		if @header_readed
			# @response = "Hello STM32F4 Wifi mruby powered Web Server\r\n" + 
			#    "you access" + @url + "\r\n"
			body = Router.instance.process_request(@url)
			status_line = "HTTP/1.1 200 OK\r\n" 
			header = "Content-Length: #{body.size}\r\n"
			@response = status_line + header + "\r\n" + body
		end

	end

	def response
		@response
	end

	def response?
		!@response.empty?
	end

end


class WebServer
	def initialize
		@requests = {}
	end

	def add_request(cid, req)
		if (@requests[cid])
			@requests[cid].add_request(req)
		else
			@requests[cid] = Request.new
			@requests[cid].add_request(req)
		end
	end

	def remove_request(cid)
		@requests.delete(cid)
	end

	def response(cid)
		@requests[cid].response
	end

	def response?(cid)
		@requests[cid].response?
	end

end

class RequestBinding
	def initialize(url, params)
		@url = url
		@params = params
	end

	def url
		@url
	end

	def params
		@params
	end

	def request
		self
	end
end

=begin
	  ""
	    /*(:direct)
	      /pchild block
	      /pchild2 block
	        /pgrandchild block
	      /pchild3
	        /*(:pgrandchild3) block
	    /child
	      /*(:grandchild) block

=end

class Node
	attr_accessor :childs
	attr_accessor :type  #:any, or :exact
	attr_accessor :name  #name of node or param name for @type == :any
	attr_accessor :block  #may be nil

	def initialize
		@childs = []
	end

	def add_child(n)
		@childs << n
	end

	def find_any_child
		childs.reverse.find do |n|
			n.type == :any
		end
	end

	def find_exact_child(name)
		childs.reverse.find do |n|
			n.type == :exact and n.name == name
		end
	end

	def find_child(name)
		c = find_exact_child(name)
		if (c.nil?)
			c = find_any_child
		end
		c
	end
end 

class RouteError < RuntimeError ; end

class Router
	@@instance = nil
	def self.instance
		if @@instance.nil?
			@@instance = Router.new
		end
		@@instance
	end

	def initialize
		@root = Node.new
		@root.type = :exact
		@root.name = ""
	end

	def add_route(path_spec, &block)
		parts = path_spec.split("/")
		node = @root
		nest = 0
		until parts.empty?
			part = parts.shift
			next if part == ""
			nest += 1
			if part.start_with?(":")
				child = node.find_any_child
				if (child.nil?)
					#puts "  " * nest + "addint new any route #{part}"
					child = Node.new
					child.type = :any
					child.name = part[1,100]
					node.add_child(child)
				else
					if child.name != part[1,100]
						raise RouteError, "ambiguous path! current limitation: #{part}"
					end
				end
			else
				#create or pick :exact node
				child = node.find_exact_child(part)
				if (child.nil?)
					#puts "  " * nest +  "adding new exact route #{part}"		
					child = Node.new
					child.type = :exact
					child.name = part
					node.add_child(child)
				end
			end
			node = child
		end

		node.block = block
	end

	def process_request(path)
		parts = path.split("/")
		node = @root
		params = {}
		params_arr =[]		#params for pass to block

		until parts.empty?
			part = parts.shift
			next if part == ""
			node = node.find_child(part)
			if node.nil?
				raise RouteError , "path not found [404] for path:#{path}"
			end
			if node.type == :any
				params[node.name.to_sym] = part
				params_arr <<  part
			end
		end

		if (node.block.nil?)
			raise RouteError, "path does not support for path:#{path}"
		end

		rb = RequestBinding.new(path, params)
		rb.instance_exec *params_arr, &node.block

	end
end

module RouterDSL
	def get(path, &block)
		Router.instance.add_route(path, &block)
	end

	#module_function :get
end

include RouterDSL
include Arduino

LED_GREEN = 60 
LED_ORANGE = 61
LED_RED = 62
LED_BLUE = 63

[LED_GREEN, LED_ORANGE, LED_RED, LED_BLUE].each do |led|
	pinMode(led, OUTPUT);
end

html = <<EOS
	<html>
		<head><title>STM32F4-Discovery LED Controller</title></head>
		<body align=center>
		 <p>Hello mruby Wifi Web Server</p>
		 <li><a href="/control/green/on"> green on</a></li>
		 <li><a href="/control/green/off"> green off</a></li>
		 <li><a href="/control/orange/on"> orange on</a></li>
		 <li><a href="/control/orange/off"> orange off</a></li>
		 <li><a href="/control/red/on"> red on</a></li>
		 <li><a href="/control/red/off"> red off</a></li>
		 <li><a href="/control/blue/on"> blue on</a></li>
		 <li><a href="/control/blue/off"> blue off</a></li>
		</body>
	</html>
EOS

get "/" do
	html
end
 
get "/control/:color/:onoff" do |color, onoff|
	pin = case color
		when "green"
			LED_GREEN
		when "orange"
			LED_ORANGE
		when "red"
			LED_RED
		when "blue"
			LED_BLUE
		else
			return "I don't have color:#{color}"
		end

	if (onoff == "on")
		digitalWrite(pin, HIGH)
	elsif (onoff == "off")
		digitalWrite(pin, LOW)
	else
		return "Bad control:#{onoff}. should be on or off"
	end

	html
end




