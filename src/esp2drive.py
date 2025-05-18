'''
This python script is designed to display the image data from an esp32 cam and capture representative images to train machine learning models with
'''

import cv2
import urllib.request
import numpy as np
import time
from pydrive.drive import GoogleDrive
from pydrive.auth import GoogleAuth

url='http://192.168.0.27:81' ## This url and port must be updated to match the url and port hosted by the server esp32 on the same network as we are using
cv2.namedWindow("live transmission", cv2.WINDOW_AUTOSIZE)
 
count=0
 
 
gauth = GoogleAuth()
gauth.LocalWebserverAuth()
drive = GoogleDrive(gauth)
folder ="1cStPpoSzkGE-nMfg-n3puiXZCN7JuziS" #please change this value according to your folder
 
while True:
    img_resp=urllib.request.urlopen(url)
    imgnp=np.array(bytearray(img_resp.read()),dtype=np.uint8)
    frame=cv2.imdecode(imgnp,-1)
    
    cv2.imshow("live transmission", frame)
 
    key=cv2.waitKey(5)
    
    if key==ord('k'):
        count+=1
        t=str(count)+'.png'
        cv2.imwrite(t,frame)
        print("image saved as: "+t)
        f= drive.CreateFile({'parents':[{'id':folder}],'title':t})
        f.SetContentFile(t)
        f.Upload()
        print("image uploaded as: "+t)
        
    if key==ord('q'):
        break
    else:
        continue
 
cv2.destroyAllWindows()