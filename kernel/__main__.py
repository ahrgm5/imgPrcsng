from ipykernel.kernelapp import IPKernelApp
from kernel import CGDBKernel
 
IPKernelApp.launch_instance(kernel_class=CGDBKernel)