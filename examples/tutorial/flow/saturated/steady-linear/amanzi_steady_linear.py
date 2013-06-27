import matplotlib.pyplot as plt
import numpy

from amanzi_xml.observations.ObservationXML import ObservationXML as ObsXML
from amanzi_xml.observations.ObservationData import ObservationData as ObsDATA
import amanzi_xml.utils.search as search




# load input xml file
#  -- create an ObservationXML object
def loadInputXML(filename):
    Obs_xml = ObsXML(filename)
    Obs_xml.getAllCoordinates()
    return Obs_xml
            
# load the data file
#  -- use above xml object to get observation filename
#  -- create an ObservationData object
#  -- load the data using this object

def loadDataFile(Obs_xml):
    output_file =  Obs_xml.getObservationFilename()
    Obs_data = ObsDATA(output_file)
    Obs_data.getObservationData()
    return Obs_data

def plotExampleObservations(Obs_xml, Obs_data, axes1):
    #=== SPECIAL CODE ==== ONLY EXAMPLE 1 
    # Collect the used z-values
    z_vals = [coord[2] for coord in Obs_xml.coordinates]
    z_vals = set(z_vals)
    colors = ['r','b','g']
    cmap = dict((zval,color) for (zval,color) in zip(z_vals, colors))
    
    
    # -- grab the right coordinate and value
    # -- call ObservationPlotter to plot with format="bo"
    for i, coord in enumerate(Obs_xml.coordinates):
        print type(coord[0])
        print type(Obs_data.pressure_value[i])
        axes1.scatter([coord[0]], [Obs_data.pressure_value[i]], c=cmap[coord[2]], marker='s',s=50)

    axes1.set_xlabel('Distance in x-direction [meter]')
    axes1.set_ylabel('Pressure [Pa]')
    axes1.set_title('Aqueous Pressure vs Distance')
    
    # ax = plt.subplot(111, frame_on = False)
    # ax.xaxis.set_visible(False)
    # ax.yaxis.set_visible(False)
    return cmap

def plotExampleModel(filename, cmap, axes1, Obs_xml, Obs_data):
    import model.steady_linear
    mymodel = model.steady_linear.createFromXML(filename)
    table_values = []

    x = numpy.linspace(mymodel.x0,mymodel.x1,11)
    coords = numpy.zeros((11,2))
    coords[:,0] = x

    for (z_val, color) in cmap.iteritems():
        coords[:,1] = z_val
        
        pres = mymodel.run(coords)
        axes1.plot(x,pres,color,label='$z = %0.2f m$'%z_val)
        axes1.legend(loc="upper right" , fancybox = True , shadow = True)
       
       

    coordinates = numpy.array(Obs_xml.coordinates)
    pres_analytic = mymodel.run(coordinates[:,[0,2]])
    pressure_analytic = pres_analytic.tolist()
    print pressure_analytic
    
    for i, coord in enumerate(Obs_xml.coordinates):
        table_values.append([coord[0] , coord[2] , format(Obs_data.pressure_value[i], "0.0f"), format(pressure_analytic[i], "0.0f")])
        
    print table_values
   

    col_labels = ['x [m]', 'z [m]' ,'Pressure(Amanzi) [Pa]', 'Pressure (expected value) [Pa]', 'Pressure (analytic)[Pa]']
    ax = plt.subplot(111, frame_on = False)
    
    ax.xaxis.set_visible(False)
    ax.yaxis.set_visible(False)
    the_table = plt.table(cellText = table_values, colLabels = col_labels, cellLoc = 'center', colLoc = 'center', loc = 'best')
    properties = the_table.properties()
    
    the_table.auto_set_font_size(False)
    the_table.set_fontsize(14)
    the_table.scale(1,1)    
        


if __name__ == "__main__":

    import os
    import run_amanzi

    input_filename = os.path.join("..","amanzi_steady_linear.xml")

    CWD = os.getcwd()

    #set up the run directory and cd into it
    run_directory = os.path.join(CWD,"output")
    if os.path.isdir(run_directory):
        [os.remove(os.path.join(run_directory,f)) for f in os.listdir(run_directory)]
    else:
        os.mkdir(run_directory)
         
    os.chdir(run_directory)

    try:
        run_amanzi.run_amanzi('../amanzi_steady_linear.xml')
        obs_xml=loadInputXML(input_filename)
        obs_data=loadDataFile(obs_xml)

        fig1= plt.figure()
        fig2 = plt.figure()
        fig2.set_size_inches(3, 2)
        axes1=fig1.add_axes([.1,.1,.8,.8])
       
        cmap = plotExampleObservations(obs_xml,obs_data, axes1)
        plotExampleModel(input_filename, cmap, axes1, obs_xml, obs_data)
        
        plt.show()

    finally:
        os.chdir(CWD)

