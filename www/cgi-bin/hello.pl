#!/usr/bin/perl
print "Content-Type: text/plain\n";
print "\n";
print "Hello from CGI (perl)\n";
print "METHOD=$ENV{REQUEST_METHOD}\n";
print "QS=$ENV{QUERY_STRING}\n";
