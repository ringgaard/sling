import socket

noncaching_getaddrinfo = socket.getaddrinfo
dns_cache = {}

def caching_getaddrinfo(*args):
  res = dns_cache.get(args, None)
  if res != None:
    #print "cached", args, res
    return res

  res = noncaching_getaddrinfo(*args)
  #print "dns lookup", args, res
  dns_cache[args] = res
  return res

socket.getaddrinfo = caching_getaddrinfo

def invalidate(hostname):
  if hostname in dns_cache:
    del dns_cache[hostname]

