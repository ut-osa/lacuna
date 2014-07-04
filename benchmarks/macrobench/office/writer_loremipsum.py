#!/usr/bin/python
import os, sys, time, subprocess
import uno, itertools

from com.sun.star.text.ControlCharacter import PARAGRAPH_BREAK
from com.sun.star.text.TextContentAnchorType import AS_CHARACTER, AT_PARAGRAPH
from com.sun.star.awt import Size


sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), os.pardir, "web", "bench_scripts")))

from privos_tools import Logger
 
# kill any lingering soffice process.
os.system("killall soffice.bin")
if os.system("ps -Csoffice.bin") == 0:
	print "Please close existing libreoffice instance"
	sys.exit(1)

# spawn a process and wait for its initialization
pid = subprocess.Popen(['soffice', '--norestore', '--nosplash', '--writer', '--accept=socket,host=localhost,port=2002;urp;'])
#pid = subprocess.Popen(['soffice', '"-accept=socket,host=localhost,port=2002;urp;"'])
time.sleep(10)
 
localContext = uno.getComponentContext()
resolver = localContext.ServiceManager.createInstanceWithContext(
    "com.sun.star.bridge.UnoUrlResolver", localContext )
ctx = resolver.resolve( "uno:socket,host=localhost,port=2002;urp;StarOffice.ComponentContext" )
smgr = ctx.ServiceManager
desktop = smgr.createInstanceWithContext( "com.sun.star.frame.Desktop",ctx)
model = desktop.getCurrentComponent()
text = model.Text
cursor = text.createTextCursor()

# 150 paragraphs, 14173 words, 95159 byte 
textToInsert = open("data/lorem_ipsum.txt").read()

numImage = 32
imageInsertPoint = len(textToInsert) / numImage

def insertImage(doc, text, color):
        oShape = doc.createInstance( "com.sun.star.text.TextGraphicObject" ) 
        oShape.GraphicURL = "file://" + os.path.abspath(os.path.join(os.path.dirname(__file__), "data", "%s.gif"%(("%02x"%color)*3,)))
        oShape.Width = 6000
        oShape.Height = 4000
	oShape.AnchorType = AT_PARAGRAPH
	oShape.SurroundAnchorOnly = False 
#	oShape.Surround = WRAP_NONE 
	oShape.TopMargin = 1000 
	oShape.BottomMargin = 1000 

	text.insertControlCharacter(cursor, PARAGRAPH_BREAK, False)
        text.insertTextContent(text.getEnd(),oShape,uno.Bool(0))
	text.insertControlCharacter(cursor, PARAGRAPH_BREAK, False)

# 0 means not replacing any place selected
# this takes around 3min 
beginTime = time.time()
for char, i in itertools.izip(textToInsert, itertools.count()):
        if (i % imageInsertPoint) == 0 and i/imageInsertPoint*8 < 256:
                insertImage(model, text, i/imageInsertPoint*8)
	text.insertString( cursor, char, 0 )
 
""" Do a nasty thing before exiting the python process. In case the
 last call is a one-way call (e.g. see idl-spec of insertString),
 it must be forced out of the remote-bridge caches before python
 exits the process. Otherwise, the one-way call may or may not reach
 the target object.
 I do this here by calling a cheap synchronous call (getPropertyValue)."""
ctx.ServiceManager

endTime = time.time()

logger = Logger("libreoffice")
logger.log("95159 byte (150 paragraph) lorem ipsum time %f sec"%( endTime-beginTime ))

os.system("killall soffice.bin")
