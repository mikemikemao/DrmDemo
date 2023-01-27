/*
 * ToolUnits.h
 *
 *  Created on: 2022��7��26��
 *      Author: maochaoqun
 */

#ifndef TOOLS_TOOLUNITS_H_
#define TOOLS_TOOLUNITS_H_
#include "stdint.h"

class ToolUnits {
public:
	static int SaveData(char *szFileName,unsigned char *iBuf,unsigned int iLen);
	static int SaveDataApp(char *szFileName, unsigned char *iBuf,unsigned int iLen);
	static int GetDataSize(char *szFileName);
	static int ReadData(char *szFileName,unsigned char *oBuf,int iLen);
	static int GetCurrentPath(char* exeFullPath,int pathLen);
	static int64_t systemnanotime();
};

#endif /* TOOLS_TOOLUNITS_H_ */
