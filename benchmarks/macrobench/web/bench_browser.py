#!/usr/bin/python

url = """http://www.longhorndelivery.com/"""

from selenium import webdriver
from selenium.common.exceptions import NoSuchElementException
from selenium.webdriver.support import select

import time, sys, timeit
from privos_tools import Logger,PrivosPerfTest

list_url = ["http://www.google.com",
            "http://www.facebook.com",
            "http://www.youtube.com",
            "http://www.yahoo.com",
            "http://www.amazon.com",
            "http://www.wikipedia.org",
            "http://www.ebay.com",
            "http://www.twitter.com",
            "http://www.craigslist.org",
            "http://www.linkedin.com",
            "http://www.blogspot.com",
            "http://www.live.com",
            "http://www.msn.com",
            "http://www.go.com",
            "http://www.bing.com",
            "http://www.pinterest.com",
            "http://www.paypal.com",
            "http://www.tumblr.com",
            "http://www.aol.com",
            "http://www.cnn.com"]
logger = Logger("web_browse")

browser = webdriver.Firefox()
browser.get(url)

dic_url = {}

def navigateURLForTiming():
    browser.get(url)

for url in list_url:
    browser.get(url)
    dic_url[url] = {"time":timeit.timeit(navigateURLForTiming, number=1)}

for url in list_url:
    logger.log("%s\t%s\n" % (url, dic_url[url]))
    
