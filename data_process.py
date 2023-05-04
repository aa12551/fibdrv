import numpy as np
import sys

result = []
result_split = []

def outlier_filter(datas, threshold = 2):
    z = np.abs((datas - datas.mean()) / datas.std())
    return datas[z < threshold]

def data_processing(data_set):
	catgories = data_set.shape[0]
	samples = data_set.shape[1]
	final = np.zeros(catgories)
	for c in range(catgories):
		final[c] = outlier_filter(data_set[c]).mean()
	return final


# open the file
filename = sys.argv[1]
with open(filename, "r") as f:
    tmp = f.readline()
    while(tmp):
        result.append(tmp)
        tmp = f.readline()
    f.close()
    
# To know the size of the data
a = []
a.append(result[0].split(' '))
n = len(result)
t = len(a[0]) - 2
dics = np.zeros((n,t-2))

j = 0;
#split data by space
for r in result:
	result_split.append(r.split(' '))
	for i in range (1,t-2):
		dics[j][i-1] = int(result_split[j][i]) 
	j = j+1;
	
# Do data process       
data = data_processing(dics)


path = "fibonacci " + filename
f = open(path, 'w')
for i in range(data.shape[0]):
	f.write(str(data[i]));
	f.write("\n");
f.close()
