#!/usr/bin/env bash

nc $1 8000 << EOF
GET /1 HTTP/1.1
User-Agent: Opera/9.25 (Windows NT 5.1; U; en)
Host: harvard:8000
Accept: text/html, application/xml;q=0.9, application/xhtml+xml, image/png, image/jpeg, image/gif, image/x-xbitmap, */*;q=0.1
Accept-Language: en-US,en;q=0.9
Accept-Charset: iso-8859-1, utf-8, utf-16, *;q=0.1
Accept-Encoding: deflate, gzip, x-gzip, identity, *;q=0
Connection: Keep-Alive
EOF
