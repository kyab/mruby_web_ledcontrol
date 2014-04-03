#To (re)compile C bytecode:
#
#/path/to/mruby/bin/mrbc -Bblinker -oblinker.c blinker.rb
#

SerialHost = Serial3

class Blinker
	include Arduino
	attr_accessor :interval ,:pin
	
	def initialize(pin,interval_ms)
		SerialHost.println("Blinker initialized(from mruby)")
		@pin = pin
		@interval = interval_ms
		pinMode(@pin, OUTPUT)
	end

	def run
		#SerialHost.println("blink! discovery! WIFI!")

		digitalWrite(@pin, HIGH)
		delay(@interval)
		digitalWrite(@pin, LOW)
		delay(@interval)
	end
end
