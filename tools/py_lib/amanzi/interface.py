# ############################################################################ #

import os, sys


from amanzi import input_tree

# ############################################################################ #

class AmanziInterface:

  def __init__(self,binary=None,input=None,output=None,error=None,mpiexec='mpiexec'):

    self.binary=binary
    self.mpiexec=mpiexec
    self.input=input
    self.output=output
    self.error=error

    self.nprocs=0
    self.exit_code=None
    self.args=[]

  def run(self):
    import time
    import signal
    try:
      import subprocess
    except ImportError:
      print 'Failed to import module subprocess'
      print 'Module is avail in Python 2.4 and higher'
      print 'Current Python version ' + str(sys.version)
      raise
    except:
      print "Unknown error:", sys.exc_info()
      raise

    # Check the files
    if not self._check_binary():
      raise ValueError, 'Can not run binary'

    if not self._check_input():
      raise ValueError, 'Invalid input'

    if self.nprocs > 0:
      if not self._check_mpiexec:
        raise ValueError, 'Can not launch a parallel executable'

    # Open files to redirect STDOUT and STDERR
    try:
      stdout_fh = open(self.output,'w')
    except (ValueError,TypeError):
      stdout_fh=None
    except IOError:
      raise IOError, 'Failed to open ' + self.output
    except:
      print "Unknown exception:", sys.exc_info()[0]
      raise

    try:
      stderr_fh = open(self.error,'w')
    except (ValueError,TypeError):
      stderr_fh=None
    except IOError:
      raise IOError, 'Failed to open ' + self.error
    except:
      print 'Unknown exception'
      raise

    # Define the arguments and execuables
    if self.nprocs == 0:
      executable=self.binary
      args=[executable, self.input_option()]
    else:
      executable=self.mpiexec
      args=[executable, '-n', str(self.nprocs), self.binary, self.input_option()]
  
    # Open a pipe, catch a CTRL-C shut down the pipe
    try: 
      pipe = subprocess.Popen(args,executable=executable,bufsize=-1,stdout=stdout_fh,stderr=stderr_fh)
    except ValueError:
      raise ValueError, 'Popen called with incorrect arguments'
    else:
      try:
          pipe.wait()
      except KeyboardInterrupt:
	try:
	  kill_signal=signal.SIGKILL
	except AttributeError:
	  kill_signal=signal.SIGTERM
	print 'Sending SIGTERM to Amanzi run PID='+str(pipe.pid)
	os.kill(pipe.pid,signal.SIGTERM)
	time.sleep(1)
	while pipe.poll() == None:
	  print 'PID='+str(pipe,pid)+' still alive. Sending signal (SIGKILL) '+str(kill_signal)
	  os.kill(pipe.pid,kill_signal)
	  time.sleep(5)

    # Set the return code 
    self.exit_code=pipe.returncode

    # Flush and close the files
    try:
      stdout_fh.flush()
    except:
      pass
    else:
      stdout_fh.close()
    
    try:
      stderr_fh.flush()
    except:
      pass
    else:
      stderr_fh.close()

    return pipe.returncode

  def input_option(self):
    return '--xml_file='+self.input

  def _check_mpiexec(self):
    from os import path,access, R_OK, X_OK
    
    try:
      exists=os.path.exists(self.mpiexec)
    except TypeError:
      print 'MPI launch command is not defined'
      raise
   
    ok_flag=False
    if exists and path.isfile(self.mpiexec) and access(self.mpiexec,R_OK|X_OK):
      ok_flag=True
    else:
      print amanzi.mpiexec + ' does not exist or is not readable or is not executbale'

    return ok_flag


  def _check_binary(self):
    from os import path,access, R_OK, X_OK
    
    try:
      exists=os.path.exists(self.binary)
    except TypeError:
      print 'Amanzi binary is not defined'
      raise
   
    ok_flag=False
    if exists and path.isfile(self.binary) and access(self.binary,R_OK|X_OK):
      ok_flag=True
    else:
      print self.binary + ' does not exist or is not readable'

    return ok_flag  

  def _check_input(self):
    from os import path,access, R_OK
    
    try:
      exists=os.path.exists(self.input)
    except TypeError:
      print 'Input file is not defined'
      raise
   
    ok_flag=False
    if exists and path.isfile(self.input) and access(self.input,R_OK):
      ok_flag=True
    else:
      print amanzi.input+ ' does not exist or is not readable'

    return ok_flag  

if __name__ == '__main__':

  try:
    amanzi=AmanziInterface('duh.xml')
  except ValueError:
    print 'Caught the invalid input error'
    
