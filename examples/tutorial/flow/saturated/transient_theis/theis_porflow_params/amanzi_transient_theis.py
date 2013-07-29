import matplotlib.pyplot as plt
import numpy
import math
from amanzi_xml.observations.ObservationXML import ObservationXML as ObsXML
from amanzi_xml.observations.ObservationData import ObservationData as ObsDATA
import amanzi_xml.utils.search as search
import model_theis 

# load input xml file
#  -- create an ObservationXML object
def loadInputXML(filename):
    Obs_xml = ObsXML(filename)
    return Obs_xml

# load the data file
#  -- use above xml object to get observation filename
#  -- create an ObservationData object
#  -- load the data using this object

def loadDataFile(Obs_xml):
    output_file =  Obs_xml.getObservationFilename()
    Obs_data = ObsDATA("amanzi-output/"+output_file)
    Obs_data.getObservationData()
    coords = Obs_xml.getAllCoordinates()

    for obs in Obs_data.observations.itervalues():
        region = obs.region
        obs.coordinate = coords[region]
    
    return Obs_data

def plotTheisObservations(Obs_xml, Obs_data, axes1):
    #=== SPECIAL CODE === Theis EXAMPLE === Water Table is 12.0 m 
    r_vals =[]
    for obs in Obs_data.observations.itervalues():
        r_vals.append(obs.coordinate[0])
    r_vals = set(r_vals)
    r_vals=list(r_vals)
    r_vals.sort()
    colors = ['b','g','r']
    cmap = dict((rval,color) for (rval,color) in zip(r_vals, colors))
    print cmap

    for obs in Obs_data.observations.itervalues():
        color = cmap[obs.coordinate[0]]
        pres0 = 101325 -9806.65 * (obs.coordinate[2] - 20)
        pres_drop = (pres0 - numpy.array([obs.data]))
        drawdown = pres_drop / 9806.65
        axes1.scatter(obs.times, drawdown, marker='s', s=25, c=color)
        
    return cmap

def plotTheisAnalytic(filename, cmap, axes1, Obs_xml ,Obs_data):
    mymodel = model_theis.createFromXML(filename)
    tindex = numpy.arange(125)
    times = []
    table_values = []
    press_amanzi = []
    press_analytic=[]
    axes1.set_ylabel('Drawdown [m]')
    axes1.set_xlabel('Time after pumping [s]')
    axes1.set_title('Drawdown vs Time after Pumping')
    
    for i in tindex:
        times.append(1+math.exp(float(i)*(i+1)/(10.3*len(tindex))))
    
    for rad in mymodel.r:
        r= abs(rad)
        drawdown=mymodel.runForFixedRadius(times,r)
        axes1.plot(times,drawdown, label='$r=%0.1f m$'%r)
        press_analytic.append([r,drawdown])
    
    for obs in Obs_data.observations.itervalues():
        press_amanzi.append(obs.data)
    
    for press1, press2 in zip(press_analytic,press_amanzi):
        table_values.append([press1[0],press1[1],press2])
        
    axes1.legend(title='Theis Solution',loc='lower right', fancybox=True, shadow=True)

if __name__ == "__main__":

    import os
    import run_amanzi

    input_filename =os.path.join("amanzi_transient_theis.xml")

    CWD = os.getcwd()
    try: 
        run_amanzi.run_amanzi('../'+input_filename)
        obs_xml=loadInputXML(input_filename)
        obs_data=loadDataFile(obs_xml)

        fig1= plt.figure()
        axes1=fig1.add_axes([.1,.1,.8,.8])
       
        cmap = plotTheisObservations(obs_xml,obs_data,axes1)
        plotTheisAnalytic(input_filename,cmap,axes1,obs_xml,obs_data)
        
        plt.show()

    finally:
        os.chdir(CWD)


