import numpy as np
import matplotlib.pyplot as plt
import matplotlib.dates as mdates

# 2015-11-11T20:29:47.477Z

def graph():
    energy, timestamp = np.loadtxt('stream_9JxQdlMzvXT8gLvAj81x.csv', delimiter = ',', unpack = True, converters = {1: mdates.strpdate2num('%Y-%m-%dT%H:%M:%S.%fZ')})
    
    fig = plt.figure()
    
    ax1 = fig.add_subplot(1,1,1, axisbg='white')
    
    plt.plot_date(x=timestamp, y=energy, fmt='-')
    
    plt.title('Energyplot')
    plt.ylabel('Energy')
    plt.xlabel('Timestamp')
    plt.show()

graph()
