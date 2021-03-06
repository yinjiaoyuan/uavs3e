/**************************************************************************************
 * Copyright (C) 2018-2019 uavs3e project
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the Open-Intelligence Open Source License V1.1.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * Open-Intelligence Open Source License V1.1 for more details.
 *
 * You should have received a copy of the Open-Intelligence Open Source License V1.1
 * along with this program; if not, you can download it on:
 * http://www.aitisa.org.cn/uploadfile/2018/0910/20180910031548314.pdf
 *
 * For more information, contact us at rgwang@pkusz.edu.cn.
 **************************************************************************************/

#include "define.h"

#define ROUND(a)  (((a) < 0)? (int)((a) - 0.5) : (int)((a) + 0.5))
#define REG              0.0001
#define REG_SQR          0.0000001

//enc_alf_var_t *Enc_ALF;

#define Clip_post(high,val) ((val > high)? high: val)

void copyToImage(pel *pDest, pel *pSrc, int stride_in, int img_height, int img_width, int formatShift)
{
    int j, width, height;
    pel *pdst;
    pel *psrc;
    height = img_height >> formatShift;
    width = img_width >> formatShift;
    psrc = pSrc;
    pdst = pDest;
    for (j = 0; j < height; j++) {
        memcpy(pdst, psrc, width * sizeof(pel));
        pdst = pdst + stride_in;
        psrc = psrc + stride_in;
    }
}

void copyOneAlfBlk(pel *picDst, pel *picSrc, int stride, int ypos, int xpos, int height, int width, int isAboveAvail, int isBelowAvail)
{
    int posOffset = (ypos * stride) + xpos;
    pel *pelDst;
    pel *pelSrc;
    int j;
    int startPos = isAboveAvail ? (ypos - 4) : ypos;
    int endPos = isBelowAvail ? (ypos + height - 4) : ypos + height;
    posOffset = (startPos * stride) + xpos;
    pelDst = picDst + posOffset;
    pelSrc = picSrc + posOffset;
    for (j = startPos; j < endPos; j++) {
        memcpy(pelDst, pelSrc, sizeof(pel)*width);
        pelDst += stride;
        pelSrc += stride;
    }
}

long long xCalcSSD(enc_alf_var_t *Enc_ALF, pel *pOrg, int i_org, pel *pCmp, int iWidth, int iHeight, int iStride)
{
    long long uiSSD = 0;
    int x, y;
    unsigned int  uiShift = Enc_ALF->m_uiBitIncrement << 1;
    int iTemp;
    for (y = 0; y < iHeight; y++) {
        for (x = 0; x < iWidth; x++) {
            iTemp = pOrg[x] - pCmp[x];
            uiSSD += (iTemp * iTemp) >> uiShift;
        }
        pOrg += i_org;
        pCmp += iStride;
    }
    return uiSSD;;
}

long long calcAlfLCUDist(enc_pic_t *ep, int compIdx, int lcuAddr, int ctuYPos, int ctuXPos, int ctuHeight,
    int ctuWidth, BOOL isAboveAvail, pel *picSrc, int i_src, pel *picCmp, int stride, int formatShift)
{
    enc_alf_var_t *Enc_ALF = &ep->Enc_ALF;
    long long dist = 0;
    int  ypos, xpos, height, width;
    pel *pelCmp;
    pel *pelSrc;

    switch (compIdx) {
    case U_C:
    case V_C: {
        ypos = (ctuYPos >> formatShift);
        xpos = (ctuXPos >> formatShift);
        height = (ctuHeight >> formatShift);
        width = (ctuWidth >> formatShift);

        if (isAboveAvail) {
            ypos -= 4;
        }

        pelCmp = picCmp + ypos * stride + xpos;
        pelSrc = picSrc + ypos * i_src + xpos;
        dist += xCalcSSD(Enc_ALF, pelSrc, i_src, pelCmp, width, height, stride);
    }
              break;
    case Y_C: {
        ypos = (ctuYPos >> formatShift);
        xpos = (ctuXPos >> formatShift);
        height = (ctuHeight >> formatShift);
        width = (ctuWidth >> formatShift);

        pelCmp = picCmp + ypos * stride + xpos;
        pelSrc = picSrc + ypos * i_src + xpos;

        dist += xCalcSSD(Enc_ALF, pelSrc, i_src, pelCmp, width, height, stride);
    }
              break;
    default: {
        printf("not a legal component ID for ALF \n");
        assert(0);
        exit(-1);
    }
    }
    return dist;
}

void mergeFrom(enc_alf_corr_t *dst, enc_alf_corr_t *src, int *mergeTable, BOOL doPixAccMerge)
{
    int numCoef = ALF_MAX_NUM_COEF;
    double(*srcE)[ALF_MAX_NUM_COEF], (*dstE)[ALF_MAX_NUM_COEF];
    double *srcy, *dsty;
    int maxFilterSetSize, j, i, varInd, filtIdx;
    assert(dst->componentID == src->componentID);

    switch (dst->componentID) {
    case U_C:
    case V_C: {
        srcE = src->ECorr[0];
        dstE = dst->ECorr[0];
        srcy = src->yCorr[0];
        dsty = dst->yCorr[0];
        for (j = 0; j < numCoef; j++) {
            for (i = 0; i < numCoef; i++) {
                dstE[j][i] += srcE[j][i];
            }
            dsty[j] += srcy[j];
        }
        if (doPixAccMerge) {
            dst->pixAcc[0] = src->pixAcc[0];
        }
    }
              break;
    case Y_C: {
        maxFilterSetSize = (int)NO_VAR_BINS;
        for (varInd = 0; varInd < maxFilterSetSize; varInd++) {
            filtIdx = (mergeTable == NULL) ? (0) : (mergeTable[varInd]);
            srcE = src->ECorr[varInd];
            dstE = dst->ECorr[filtIdx];
            srcy = src->yCorr[varInd];
            dsty = dst->yCorr[filtIdx];
            for (j = 0; j < numCoef; j++) {
                for (i = 0; i < numCoef; i++) {
                    dstE[j][i] += srcE[j][i];
                }
                dsty[j] += srcy[j];
            }
            if (doPixAccMerge) {
                dst->pixAcc[filtIdx] += src->pixAcc[varInd];
            }
        }
    }
              break;
    default: {
        printf("not a legal component ID\n");
        assert(0);
        exit(-1);
    }
    }
}

void predictALFCoeff(int(*coeff)[ALF_MAX_NUM_COEF], int numCoef, int numFilters)
{
    int g, pred, sum, i;
    for (g = 0; g < numFilters; g++) {
        sum = 0;
        for (i = 0; i < numCoef - 1; i++) {
            sum += (2 * coeff[g][i]);
        }
        pred = (1 << ALF_NUM_BIT_SHIFT) - (sum);
        coeff[g][numCoef - 1] = coeff[g][numCoef - 1] - pred;
    }
}



void deriveBoundaryAvail(enc_pic_t *ep, int numLCUInPicWidth, int numLCUInPicHeight, int ctu, BOOL *isLeftAvail,
    BOOL *isRightAvail, BOOL *isAboveAvail, BOOL *isBelowAvail, BOOL *isAboveLeftAvail,
    BOOL *isAboveRightAvail)
{
    int  numLCUsInFrame = numLCUInPicHeight * numLCUInPicWidth;
    int  lcuHeight = 1 << ep->info.log2_max_cuwh;
    int  lcuWidth = lcuHeight;
    int  NumCUInFrame;
    int  pic_x;
    int  pic_y;
    int  mb_x;
    int  mb_y;
    int  mb_nr;
    int  i_scu = ep->info.i_scu;
    int  cuCurrNum;
    int  cuCurr;
    int  cuLeft;
    int  cuRight;
    int  cuAbove;
    int  cuAboveLeft;
    int  cuAboveRight;
    int curSliceNr, neighorSliceNr;
    int cross_patch_flag = ep->info.sqh.filter_cross_patch;
    NumCUInFrame = numLCUInPicHeight * numLCUInPicWidth;
    pic_x = (ctu % numLCUInPicWidth) * lcuWidth;
    pic_y = (ctu / numLCUInPicWidth) * lcuHeight;

    mb_x = pic_x / MIN_CU_SIZE;
    mb_y = pic_y / MIN_CU_SIZE;
    mb_nr = mb_y * i_scu + mb_x;
    cuCurrNum = mb_nr;
    *isLeftAvail = (ctu % numLCUInPicWidth != 0);
    *isRightAvail = (ctu % numLCUInPicWidth != numLCUInPicWidth - 1);
    *isAboveAvail = (ctu >= numLCUInPicWidth);
    *isBelowAvail = (ctu < numLCUsInFrame - numLCUInPicWidth);
    *isAboveLeftAvail = *isAboveAvail && *isLeftAvail;
    *isAboveRightAvail = *isAboveAvail && *isRightAvail;
    s8 *map_patch = ep->map.map_patch;
    cuCurr = (cuCurrNum);
    cuLeft = *isLeftAvail ? (cuCurrNum - 1) : -1;
    cuRight = *isRightAvail ? (cuCurrNum + (lcuWidth >> MIN_CU_LOG2)) : -1;
    cuAbove = *isAboveAvail ? (cuCurrNum - i_scu) : -1;
    cuAboveLeft = *isAboveLeftAvail ? (cuCurrNum - i_scu - 1) : -1;
    cuAboveRight = *isAboveRightAvail ? (cuCurrNum - i_scu + (lcuWidth >> MIN_CU_LOG2)) : -1;
    if (!cross_patch_flag) {
        *isLeftAvail = *isRightAvail = *isAboveAvail = FALSE;
        *isAboveLeftAvail = *isAboveRightAvail = FALSE;
        curSliceNr = map_patch[cuCurr];
        if (cuLeft != -1) {
            neighorSliceNr = map_patch[cuLeft];
            if (curSliceNr == neighorSliceNr) {
                *isLeftAvail = TRUE;
            }
        }
        if (cuRight != -1) {
            neighorSliceNr = map_patch[cuRight];
            if (curSliceNr == neighorSliceNr) {
                *isRightAvail = TRUE;
            }
        }
        if (cuAbove != -1) {
            neighorSliceNr = map_patch[cuAbove];
            if (curSliceNr == neighorSliceNr) {
                *isAboveAvail = TRUE;
            }
        }
        if (cuAboveLeft != -1) {
            neighorSliceNr = map_patch[cuAboveLeft];
            if (curSliceNr == neighorSliceNr) {
                *isAboveLeftAvail = TRUE;
            }
        }
        if (cuAboveRight != -1) {
            neighorSliceNr = map_patch[cuAboveRight];
            if (curSliceNr == neighorSliceNr) {
                *isAboveRightAvail = TRUE;
            }
        }
    }
}




/*
*************************************************************************
* Function: Calculate the correlation matrix for Luma
*************************************************************************
*/
void calcCorrOneCompRegionLuma(enc_alf_var_t *Enc_ALF, pel *imgOrg, int i_org, pel *imgPad, int stride, int yPos, int xPos, int height, int width
                               , double eCorr[NO_VAR_BINS][ALF_MAX_NUM_COEF][ALF_MAX_NUM_COEF], double yCorr[NO_VAR_BINS][ALF_MAX_NUM_COEF],
                               double *pixAcc, int varInd, int isLeftAvail, int isRightAvail, int isAboveAvail,
                               int isBelowAvail, int isAboveLeftAvail, int isAboveRightAvail)
{
    int xPosEnd = xPos + width;
    int N = ALF_MAX_NUM_COEF;
    int startPosLuma = isAboveAvail ? (yPos - 4) : yPos;
    int endPosLuma = isBelowAvail ? (yPos + height - 4) : (yPos + height);
    int xOffSetLeft = isLeftAvail ? -3 : 0;
    int xOffSetRight = isRightAvail ? 3 : 0;
    int yUp, yBottom;
    int xLeft, xRight;
    int ELocal[ALF_MAX_NUM_COEF];
    pel *imgPad1, *imgPad2, *imgPad3, *imgPad4, *imgPad5, *imgPad6;
    int i, j, k, l, yLocal;
    double (*E)[ALF_MAX_NUM_COEF];
    double *yy;
    imgPad += startPosLuma * stride;
    imgOrg += startPosLuma * i_org;
    //loop region height
    for (i = startPosLuma; i < endPosLuma; i++) {
        yUp = COM_CLIP3(startPosLuma, endPosLuma - 1, i - 1);
        yBottom = COM_CLIP3(startPosLuma, endPosLuma - 1, i + 1);
        imgPad1 = imgPad + (yBottom - i) * stride;
        imgPad2 = imgPad + (yUp - i) * stride;
        yUp = COM_CLIP3(startPosLuma, endPosLuma - 1, i - 2);
        yBottom = COM_CLIP3(startPosLuma, endPosLuma - 1, i + 2);
        imgPad3 = imgPad + (yBottom - i) * stride;
        imgPad4 = imgPad + (yUp - i) * stride;
        yUp = COM_CLIP3(startPosLuma, endPosLuma - 1, i - 3);
        yBottom = COM_CLIP3(startPosLuma, endPosLuma - 1, i + 3);
        imgPad5 = imgPad + (yBottom - i) * stride;
        imgPad6 = imgPad + (yUp - i) * stride;
        //loop current ctu width
        for (j = xPos; j < xPosEnd; j++) {
            memset(ELocal, 0, N * sizeof(int));
            ELocal[0] = (imgPad5[j] + imgPad6[j]);
            ELocal[1] = (imgPad3[j] + imgPad4[j]);
            ELocal[3] = (imgPad1[j] + imgPad2[j]);
            // upper left c2
            xLeft = com_alf_check_boundary(j - 1, i - 1, xPos, yPos, xPos, startPosLuma, xPosEnd - 1,
                    endPosLuma - 1, isAboveLeftAvail, isLeftAvail, isAboveRightAvail, isRightAvail);
            ELocal[2] = imgPad2[xLeft];
            // upper right c4
            xRight = com_alf_check_boundary(j + 1, i - 1, xPos, yPos, xPos, startPosLuma, xPosEnd - 1,
                     endPosLuma - 1, isAboveLeftAvail, isLeftAvail, isAboveRightAvail, isRightAvail);
            ELocal[4] = imgPad2[xRight];
            // lower left c4
            xLeft = com_alf_check_boundary(j - 1, i + 1, xPos, yPos, xPos, startPosLuma, xPosEnd - 1,
                    endPosLuma - 1, isAboveLeftAvail, isLeftAvail, isAboveRightAvail, isRightAvail);
            ELocal[4] += imgPad1[xLeft];
            // lower right c2
            xRight = com_alf_check_boundary(j + 1, i + 1, xPos, yPos, xPos, startPosLuma, xPosEnd - 1,
                     endPosLuma - 1, isAboveLeftAvail, isLeftAvail, isAboveRightAvail, isRightAvail);
            ELocal[2] += imgPad1[xRight];
            xLeft = COM_CLIP3(xPos + xOffSetLeft, xPosEnd - 1 + xOffSetRight, j - 1);
            xRight = COM_CLIP3(xPos + xOffSetLeft, xPosEnd - 1 + xOffSetRight, j + 1);
            ELocal[7] = (imgPad[xRight] + imgPad[xLeft]);
            xLeft = COM_CLIP3(xPos + xOffSetLeft, xPosEnd - 1 + xOffSetRight, j - 2);
            xRight = COM_CLIP3(xPos + xOffSetLeft, xPosEnd - 1 + xOffSetRight, j + 2);
            ELocal[6] = (imgPad[xRight] + imgPad[xLeft]);
            xLeft = COM_CLIP3(xPos + xOffSetLeft, xPosEnd - 1 + xOffSetRight, j - 3);
            xRight = COM_CLIP3(xPos + xOffSetLeft, xPosEnd - 1 + xOffSetRight, j + 3);
            ELocal[5] = (imgPad[xRight] + imgPad[xLeft]);
            ELocal[8] = (imgPad[j]);
            yLocal = imgOrg[j];
            pixAcc[varInd] += (yLocal * yLocal);
            E = eCorr[varInd];
            yy = yCorr[varInd];
            for (k = 0; k < N; k++) {
                for (l = k; l < N; l++) {
                    E[k][l] += (double)(ELocal[k] * ELocal[l]);
                }
                yy[k] += (double)(ELocal[k] * yLocal);
            }
        }
        imgPad += stride;
        imgOrg += i_org;
    }
    for (varInd = 0; varInd < NO_VAR_BINS; varInd++) {
        E = eCorr[varInd];
        for (k = 1; k < N; k++) {
            for (l = 0; l < k; l++) {
                E[k][l] = E[l][k];
            }
        }
    }
}


/*
*************************************************************************
* Function: Calculate the correlation matrix for Chroma
*************************************************************************
*/
void calcCorrOneCompRegionChma(pel *imgOrg, int i_org, pel *imgPad, int stride, int yPos, int xPos, int height, int width,
                               double eCorr[ALF_MAX_NUM_COEF][ALF_MAX_NUM_COEF], double *yCorr, int isLeftAvail, int isRightAvail, int isAboveAvail, int isBelowAvail,
                               int isAboveLeftAvail, int isAboveRightAvail)
{
    int xPosEnd = xPos + width;
    int N = ALF_MAX_NUM_COEF;
    int startPosChroma = isAboveAvail ? (yPos - 4) : yPos;
    int endPosChroma = isBelowAvail ? (yPos + height - 4) : (yPos + height);
    int xOffSetLeft = isLeftAvail ? -3 : 0;
    int xOffSetRight = isRightAvail ? 3 : 0;
    int yUp, yBottom;
    int xLeft, xRight;
    int ELocal[ALF_MAX_NUM_COEF];
    pel *imgPad1, *imgPad2, *imgPad3, *imgPad4, *imgPad5, *imgPad6;
    int i, j, k, l, yLocal;
    imgPad += startPosChroma * stride;
    imgOrg += startPosChroma * i_org;
    for (i = startPosChroma; i < endPosChroma; i++) {
        yUp = COM_CLIP3(startPosChroma, endPosChroma - 1, i - 1);
        yBottom = COM_CLIP3(startPosChroma, endPosChroma - 1, i + 1);
        imgPad1 = imgPad + (yBottom - i) * stride;
        imgPad2 = imgPad + (yUp - i) * stride;
        yUp = COM_CLIP3(startPosChroma, endPosChroma - 1, i - 2);
        yBottom = COM_CLIP3(startPosChroma, endPosChroma - 1, i + 2);
        imgPad3 = imgPad + (yBottom - i) * stride;
        imgPad4 = imgPad + (yUp - i) * stride;
        yUp = COM_CLIP3(startPosChroma, endPosChroma - 1, i - 3);
        yBottom = COM_CLIP3(startPosChroma, endPosChroma - 1, i + 3);
        imgPad5 = imgPad + (yBottom - i) * stride;
        imgPad6 = imgPad + (yUp - i) * stride;
        for (j = xPos; j < xPosEnd; j++) {
            memset(ELocal, 0, N * sizeof(int));
            ELocal[0] = (imgPad5[j] + imgPad6[j]);
            ELocal[1] = (imgPad3[j] + imgPad4[j]);
            ELocal[3] = (imgPad1[j] + imgPad2[j]);
            // upper left c2
            xLeft = com_alf_check_boundary(j - 1, i - 1, xPos, yPos, xPos, startPosChroma, xPosEnd - 1,
                    endPosChroma - 1, isAboveLeftAvail, isLeftAvail, isAboveRightAvail, isRightAvail);
            ELocal[2] = imgPad2[xLeft];
            // upper right c4
            xRight = com_alf_check_boundary(j + 1, i - 1, xPos, yPos, xPos, startPosChroma, xPosEnd - 1,
                     endPosChroma - 1, isAboveLeftAvail, isLeftAvail, isAboveRightAvail, isRightAvail);
            ELocal[4] = imgPad2[xRight];
            // lower left c4
            xLeft = com_alf_check_boundary(j - 1, i + 1, xPos, yPos, xPos, startPosChroma, xPosEnd - 1,
                    endPosChroma - 1, isAboveLeftAvail, isLeftAvail, isAboveRightAvail, isRightAvail);
            ELocal[4] += imgPad1[xLeft];
            // lower right c2
            xRight = com_alf_check_boundary(j + 1, i + 1, xPos, yPos, xPos, startPosChroma, xPosEnd - 1,
                     endPosChroma - 1, isAboveLeftAvail, isLeftAvail, isAboveRightAvail, isRightAvail);
            ELocal[2] += imgPad1[xRight];
            xLeft = COM_CLIP3(xPos + xOffSetLeft, xPosEnd - 1 + xOffSetRight, j - 1);
            xRight = COM_CLIP3(xPos + xOffSetLeft, xPosEnd - 1 + xOffSetRight, j + 1);
            ELocal[7] = (imgPad[xRight] + imgPad[xLeft]);
            xLeft = COM_CLIP3(xPos + xOffSetLeft, xPosEnd - 1 + xOffSetRight, j - 2);
            xRight = COM_CLIP3(xPos + xOffSetLeft, xPosEnd - 1 + xOffSetRight, j + 2);
            ELocal[6] = (imgPad[xRight] + imgPad[xLeft]);
            xLeft = COM_CLIP3(xPos + xOffSetLeft, xPosEnd - 1 + xOffSetRight, j - 3);
            xRight = COM_CLIP3(xPos + xOffSetLeft, xPosEnd - 1 + xOffSetRight, j + 3);
            ELocal[5] = (imgPad[xRight] + imgPad[xLeft]);
            ELocal[8] = (imgPad[j]);
            yLocal = (int)imgOrg[j];
            for (k = 0; k < N; k++) {
                eCorr[k][k] += ELocal[k] * ELocal[k];
                for (l = k + 1; l < N; l++) {
                    eCorr[k][l] += ELocal[k] * ELocal[l];
                }
                yCorr[k] += yLocal * ELocal[k];
            }
        }
        imgPad += stride;
        imgOrg += i_org;
    }
    for (j = 0; j < N - 1; j++) {
        for (i = j + 1; i < N; i++) {
            eCorr[i][j] = eCorr[j][i];
        }
    }
}

/*
*************************************************************************
* Function: Calculate the correlation matrix for each LCU
* Input:
*  skipCUBoundaries  : Boundary skip flag
*           compIdx  : Image component index
*            lcuAddr : The address of current LCU
* (ctuXPos,ctuYPos)  : The LCU position
*          isXXAvail : The Available of neighboring LCU
*            pPicOrg : The original image buffer
*            pPicSrc : The distortion image buffer
*           stride : The width of image buffer
* Output:
* Return:
*************************************************************************
*/
void getStatisticsOneLCU(enc_alf_var_t *Enc_ALF, int compIdx, int varInd
    , int ctuYPos, int ctuXPos, int ctuHeight, int ctuWidth
    , BOOL isAboveAvail, BOOL isBelowAvail, BOOL isLeftAvail, BOOL isRightAvail
    , BOOL isAboveLeftAvail, BOOL isAboveRightAvail, enc_alf_corr_t *alfCorr, pel *pPicOrg, int i_org, pel *pPicSrc
    , int stride, int formatShift)
{
    int ypos, xpos, height, width;
    switch (compIdx) {
    case U_C:
    case V_C: {
        ypos = (ctuYPos >> formatShift);
        xpos = (ctuXPos >> formatShift);
        height = (ctuHeight >> formatShift);
        width = (ctuWidth >> formatShift);
        calcCorrOneCompRegionChma(pPicOrg, i_org, pPicSrc, stride, ypos, xpos, height, width, alfCorr->ECorr[0], alfCorr->yCorr[0],
            isLeftAvail, isRightAvail, isAboveAvail, isBelowAvail, isAboveLeftAvail, isAboveRightAvail);
    }
              break;
    case Y_C: {
        ypos = (ctuYPos >> formatShift);
        xpos = (ctuXPos >> formatShift);
        height = (ctuHeight >> formatShift);
        width = (ctuWidth >> formatShift);
        calcCorrOneCompRegionLuma(Enc_ALF, pPicOrg, i_org, pPicSrc, stride, ypos, xpos, height, width, alfCorr->ECorr, alfCorr->yCorr,
            alfCorr->pixAcc, varInd, isLeftAvail, isRightAvail, isAboveAvail, isBelowAvail, isAboveLeftAvail, isAboveRightAvail);
    }
              break;
    default: {
        printf("Not a legal component index for ALF\n");
        assert(0);
        exit(-1);
    }
    }
}


/*
*************************************************************************
* Function: Calculate the correlation matrix for image
* Input:
*             h  : CONTEXT used for encoding process
*     pic_alf_Org  : picture of the original image
*     pic_alf_Rec  : picture of the the ALF input image
*           stride : The stride of Y component of the ALF input picture
*           lambda : The lambda value in the ALF-RD decision
* Output:
* Return:
*************************************************************************
*/
void alf_get_statistics(enc_pic_t *ep, pel *imgY_org, pel **imgUV_org, int i_org, pel *imgY_Dec, pel **imgUV_Dec, int stride)
{
    enc_alf_var_t *Enc_ALF = &ep->Enc_ALF;
    BOOL  isLeftAvail, isRightAvail, isAboveAvail, isBelowAvail;
    BOOL  isAboveLeftAvail, isAboveRightAvail;
    int lcuHeight, lcuWidth, img_height, img_width;
    int ctu, NumCUInFrame, numLCUInPicWidth, numLCUInPicHeight;
    int ctuYPos, ctuXPos, ctuHeight, ctuWidth;
    int compIdx, formatShift;
    lcuHeight = 1 << ep->info.log2_max_cuwh;
    lcuWidth = lcuHeight;
    img_height = ep->info.pic_height;
    img_width = ep->info.pic_width;
    numLCUInPicWidth = img_width / lcuWidth;
    numLCUInPicHeight = img_height / lcuHeight;
    numLCUInPicWidth += (img_width % lcuWidth) ? 1 : 0;
    numLCUInPicHeight += (img_height % lcuHeight) ? 1 : 0;
    NumCUInFrame = numLCUInPicHeight * numLCUInPicWidth;

    for (ctu = 0; ctu < NumCUInFrame; ctu++) {
        int varIdx = ep->alf_var_map[ctu];

        ctuYPos = (ctu / numLCUInPicWidth) * lcuHeight;
        ctuXPos = (ctu % numLCUInPicWidth) * lcuWidth;
        ctuHeight = (ctuYPos + lcuHeight > img_height) ? (img_height - ctuYPos) : lcuHeight;
        ctuWidth = (ctuXPos + lcuWidth > img_width) ? (img_width - ctuXPos) : lcuWidth;
        deriveBoundaryAvail(ep, numLCUInPicWidth, numLCUInPicHeight, ctu, &isLeftAvail, &isRightAvail, &isAboveAvail, &isBelowAvail, &isAboveLeftAvail, &isAboveRightAvail);

        for (compIdx = 0; compIdx < N_C; compIdx++) {
            formatShift = (compIdx == Y_C) ? 0 : 1;
            memset(&Enc_ALF->m_alfCorr[ctu][compIdx], 0, sizeof(enc_alf_corr_t));
            if (compIdx == Y_C) {
                getStatisticsOneLCU(&ep->Enc_ALF, compIdx, varIdx, ctuYPos, ctuXPos, ctuHeight, ctuWidth, isAboveAvail,
                    isBelowAvail, isLeftAvail, isRightAvail
                    , isAboveLeftAvail, isAboveRightAvail, &Enc_ALF->m_alfCorr[ctu][compIdx], imgY_org, i_org, imgY_Dec, stride, formatShift);
            }
            else {
                getStatisticsOneLCU(&ep->Enc_ALF, compIdx, varIdx, ctuYPos, ctuXPos, ctuHeight, ctuWidth, isAboveAvail,
                    isBelowAvail, isLeftAvail, isRightAvail
                    , isAboveLeftAvail, isAboveRightAvail, &Enc_ALF->m_alfCorr[ctu][compIdx], imgUV_org[compIdx - U_C], i_org >> 1,
                    imgUV_Dec[compIdx - U_C], (stride >> 1), formatShift);
            }
        }
    }
}

unsigned int uvlcBitrateEstimate(int val)
{
    unsigned int length = 1;
    val++;
    assert(val);
    while (1 != val) {
        val >>= 1;
        length += 2;
    }
    return ((length >> 1) + ((length + 1) >> 1));
}
unsigned int svlcBitrateEsitmate(int val)
{
    return uvlcBitrateEstimate((val <= 0) ? (-val << 1) : ((val << 1) - 1));
}

unsigned int filterCoeffBitrateEstimate(int *coeff)
{
    unsigned int  bitrate = 0;
    int i;
    for (i = 0; i < (int)ALF_MAX_NUM_COEF; i++) {
        bitrate += (svlcBitrateEsitmate(coeff[i]));
    }
    return bitrate;
}
unsigned int ALFParamBitrateEstimate(com_alf_pic_param_t *alfParam)
{
    unsigned int  bitrate = 0; //alf enabled flag
    int noFilters, g;
    if (alfParam->alf_flag == 1) {
        if (alfParam->componentID == Y_C) {
            noFilters = alfParam->filters_per_group - 1;
            bitrate += uvlcBitrateEstimate(noFilters);
            bitrate += (4 * noFilters);
        }
        for (g = 0; g < alfParam->filters_per_group; g++) {
            bitrate += filterCoeffBitrateEstimate(alfParam->coeffmulti[g]);
        }
    }
    return bitrate;
}

unsigned int estimateALFBitrateInPicHeader(com_alf_pic_param_t *alfPicParam)
{
    //CXCTBD please help to check if the implementation is consistent with syntax coding
    int compIdx;
    unsigned int bitrate = 3; // pic_alf_enabled_flag[0,1,2]
    if (alfPicParam[0].alf_flag == 1 || alfPicParam[1].alf_flag == 1 || alfPicParam[2].alf_flag == 1) {
        for (compIdx = 0; compIdx < N_C; compIdx++) {
            bitrate += ALFParamBitrateEstimate(&alfPicParam[compIdx]);
        }
    }
    return bitrate;
}

long long xFastFiltDistEstimation(enc_alf_var_t *Enc_ALF, double ppdE[ALF_MAX_NUM_COEF][ALF_MAX_NUM_COEF], double *pdy, int *piCoeff, int iFiltLength)
{
    //static memory
    double pdcoeff[ALF_MAX_NUM_COEF];
    //variable
    int    i, j;
    long long  iDist;
    double dDist, dsum;
    unsigned int uiShift;
    for (i = 0; i < iFiltLength; i++) {
        pdcoeff[i] = (double)piCoeff[i] / (double)(1 << ((int)ALF_NUM_BIT_SHIFT));
    }
    dDist = 0;
    for (i = 0; i < iFiltLength; i++) {
        dsum = ((double)ppdE[i][i]) * pdcoeff[i];
        for (j = i + 1; j < iFiltLength; j++) {
            dsum += (double)(2 * ppdE[i][j]) * pdcoeff[j];
        }
        dDist += ((dsum - 2.0 * pdy[i]) * pdcoeff[i]);
    }
    uiShift = Enc_ALF->m_uiBitIncrement << 1;
    if (dDist < 0) {
        iDist = -(((long long)(-dDist + 0.5)) >> uiShift);
    } else { //dDist >=0
        iDist = ((long long)(dDist + 0.5)) >> uiShift;
    }
    return iDist;
}
long long estimateFilterDistortion(enc_alf_var_t *Enc_ALF, int compIdx, enc_alf_corr_t *alfCorr, int(*coeffSet)[ALF_MAX_NUM_COEF], int filterSetSize, int *mergeTable, BOOL doPixAccMerge)
{
    enc_alf_corr_t alfMerged;
    long long iDist = 0;
    memset(&alfMerged, 0, sizeof(enc_alf_corr_t));
    mergeFrom(&alfMerged, alfCorr, mergeTable, doPixAccMerge);

    for (int f = 0; f < filterSetSize; f++) {
        iDist += xFastFiltDistEstimation(Enc_ALF, alfMerged.ECorr[f], alfMerged.yCorr[f], coeffSet ? coeffSet[f] : Enc_ALF->m_coeffNoFilter, ALF_MAX_NUM_COEF);
    }
    return iDist;
}

void filterOneCTB(enc_alf_var_t *Enc_ALF, pel *pRest, pel *pDec, int varIdx, int stride, int compIdx, int bit_depth, com_alf_pic_param_t *alfParam, int ctuYPos, int ctuHeight,
    int ctuXPos, int ctuWidth, int(*coeffSet)[ALF_MAX_NUM_COEF]
    , BOOL isAboveAvail, BOOL isBelowAvail, BOOL isLeftAvail, BOOL isRightAvail, BOOL isAboveLeftAvail,
    BOOL isAboveRightAvail)
{
    //half size of 7x7cross+ 3x3square
    int  formatShift = (compIdx == Y_C) ? 0 : 1;
    int ypos, height, xpos, width;
    //derive CTB start positions, width, and height. If the boundary is not available, skip boundary samples.
    ypos = (ctuYPos >> formatShift);
    height = (ctuHeight >> formatShift);
    xpos = (ctuXPos >> formatShift);
    width = (ctuWidth >> formatShift);
    int *coef = (compIdx != Y_C) ? coeffSet[0] : coeffSet[Enc_ALF->m_varIndTab[varIdx]];

    if (isAboveAvail) {
        ypos -= 4;
        height += 4;
    }
    if (isBelowAvail) {
        height -= 4;
    }

    if (!isLeftAvail) {
        pel *p = pDec + ypos * stride;
        for (int i = 0; i < height; i++) {
            p[-1] = p[-2] = p[-3] = p[0];
            p += stride;
        }
    }
    if (!isRightAvail) {
        pel *p = pDec + ypos * stride + xpos + width - 1;
        for (int i = 0; i < height; i++) {
            p[1] = p[2] = p[3] = p[0];
            p += stride;
        }
    }

    pRest += ypos * stride + xpos;
    pDec += ypos * stride + xpos;

    uavs3e_funs_handle.alf(pRest, stride, pDec, stride, width, height, coef, bit_depth);
    uavs3e_funs_handle.alf_fix(pRest, stride, pDec, stride, width, height, coef, bit_depth);
}


/*
*************************************************************************
* Function: ALF On/Off decision for LCU
*************************************************************************
*/
double alf_decision_all_lcu(enc_pic_t *ep, lbac_t *lbac, com_alf_pic_param_t *alfPictureParam
                            , double lambda
                            , BOOL isRDOEstimate
                            , enc_alf_corr_t (*alfCorr)[N_C]
                            , pel *imgY_org, pel **imgUV_org, int i_org, pel *imgY_Dec, pel **imgUV_Dec, pel *imgY_Res, pel **imgUV_Res
                            , int stride)
{
    enc_alf_var_t *Enc_ALF = &ep->Enc_ALF;
    int bit_depth = ep->info.bit_depth_internal;
    long long  distEnc, distOff;
    double  rateEnc, rateOff, costEnc, costOff, costAlfOn, costAlfOff;
    BOOL isLeftAvail, isRightAvail, isAboveAvail, isBelowAvail;
    BOOL isAboveLeftAvail, isAboveRightAvail;
    long long  distBestPic[N_C];
    double rateBestPic[N_C];
    int compIdx, ctu, ctuYPos, ctuXPos, ctuHeight, ctuWidth, formatShift, n, stride_in, i_org_plane;
    pel *org = NULL;
    pel *pDec = NULL;
    pel *pRest = NULL;
    double lambda_luma, lambda_chroma;
    /////
    int lcuHeight, lcuWidth, img_height, img_width;
    int NumCUsInFrame, numLCUInPicWidth, numLCUInPicHeight;
    double bestCost = 0;
    lbac_t alf_sbac;
    lbac_copy(&alf_sbac, lbac);

    lcuHeight = 1 << ep->info.log2_max_cuwh;
    lcuWidth = lcuHeight;
    img_height = ep->info.pic_height;
    img_width = ep->info.pic_width;
    numLCUInPicWidth = img_width / lcuWidth;
    numLCUInPicHeight = img_height / lcuHeight;
    numLCUInPicWidth += (img_width % lcuWidth) ? 1 : 0;
    numLCUInPicHeight += (img_height % lcuHeight) ? 1 : 0;
    NumCUsInFrame = numLCUInPicHeight * numLCUInPicWidth;
    lambda_luma = lambda; //VKTBD lambda is not correct
    lambda_chroma = lambda_luma;

    int coefs[N_C][NO_VAR_BINS][ALF_MAX_NUM_COEF];

    for (compIdx = 0; compIdx < N_C; compIdx++) {
        distBestPic[compIdx] = 0;
        rateBestPic[compIdx] = 0;
        com_alf_recon_coef(&alfPictureParam[compIdx], coefs[compIdx]);
    }

    memset(Enc_ALF->m_varIndTab, 0, NO_VAR_BINS * sizeof(int));

    if (alfPictureParam[0].filters_per_group > 1) {
        for (int i = 1; i < NO_VAR_BINS; ++i) {
            if (alfPictureParam[0].filterPattern[i]) {
                Enc_ALF->m_varIndTab[i] = Enc_ALF->m_varIndTab[i - 1] + 1;
            } else {
                Enc_ALF->m_varIndTab[i] = Enc_ALF->m_varIndTab[i - 1];
            }
        }
    }

    for (ctu = 0; ctu < NumCUsInFrame; ctu++) {
        //derive CTU width and height
        ctuYPos = (ctu / numLCUInPicWidth) * lcuHeight;
        ctuXPos = (ctu % numLCUInPicWidth) * lcuWidth;
        ctuHeight = (ctuYPos + lcuHeight > img_height) ? (img_height - ctuYPos) : lcuHeight;
        ctuWidth = (ctuXPos + lcuWidth  > img_width) ? (img_width - ctuXPos) : lcuWidth;
        //if the current CTU is the starting CTU at the slice, reset cabac
        if (ctu > 0) {
            int prev_ctuYPos = ((ctu - 1) / numLCUInPicWidth) * lcuHeight;
            int prev_ctuXPos = ((ctu - 1) % numLCUInPicWidth) * lcuWidth;
            s8 *map_patch = ep->map.map_patch;
            int curr_mb_nr = (ctuYPos >> MIN_CU_LOG2) * (img_width >> MIN_CU_LOG2) + (ctuXPos >> MIN_CU_LOG2);
            int prev_mb_nr = (prev_ctuYPos >> MIN_CU_LOG2) * (img_width >> MIN_CU_LOG2) + (prev_ctuXPos >> MIN_CU_LOG2);
            int ctuCurr_sn = map_patch[curr_mb_nr];
            int ctuPrev_sn = map_patch[prev_mb_nr];
            if (ctuCurr_sn != ctuPrev_sn) {
                lbac_reset(&alf_sbac);    // init lbac for alf rdo
            }
        }
        //derive CTU boundary availabilities
        deriveBoundaryAvail(ep, numLCUInPicWidth, numLCUInPicHeight, ctu, &isLeftAvail, &isRightAvail, &isAboveAvail, &isBelowAvail, &isAboveLeftAvail, &isAboveRightAvail);

        for (compIdx = 0; compIdx < N_C; compIdx++) {
            //if slice-level enabled flag is 0, set CTB-level enabled flag 0
            if (alfPictureParam[compIdx].alf_flag == 0) {
                Enc_ALF->m_AlfLCUEnabled[ctu][compIdx] = FALSE;
                continue;
            }
            if (!isRDOEstimate) {
                formatShift = (compIdx == Y_C) ? 0 : 1;
                org = (compIdx == Y_C) ? imgY_org : imgUV_org[compIdx - U_C];
                pDec = (compIdx == Y_C) ? imgY_Dec : imgUV_Dec[compIdx - U_C];
                pRest = (compIdx == Y_C) ? imgY_Res : imgUV_Res[compIdx - U_C];
                stride_in = (compIdx == Y_C) ? (stride) : (stride >> 1);
                i_org_plane = (compIdx == Y_C) ? (i_org) : (i_org >> 1);
            } else {
                formatShift = 0;
                stride_in = 0;
                i_org_plane = 0;
                org = NULL;
                pDec = NULL;
                pRest = NULL;
            }

            //ALF on
            if (isRDOEstimate) {
                //distEnc is the estimated distortion reduction compared with filter-off case
                distEnc = estimateFilterDistortion(Enc_ALF, compIdx, &alfCorr[ctu][compIdx], coefs[compIdx],
                                                   alfPictureParam[compIdx].filters_per_group, Enc_ALF->m_varIndTab, FALSE)
                          - estimateFilterDistortion(Enc_ALF, compIdx, &alfCorr[ctu][compIdx], NULL, 1, NULL, FALSE);
            } else {
                filterOneCTB(Enc_ALF, pRest, pDec, ep->alf_var_map[ctu], stride_in, compIdx, bit_depth, &alfPictureParam[compIdx]
                             , ctuYPos, ctuHeight, ctuXPos, ctuWidth, coefs[compIdx], isAboveAvail, isBelowAvail, isLeftAvail, isRightAvail, isAboveLeftAvail,
                             isAboveRightAvail);

                distEnc = calcAlfLCUDist(ep, compIdx, ctu, ctuYPos, ctuXPos, ctuHeight, ctuWidth, isAboveAvail, org, i_org_plane, pRest, stride_in, formatShift);
                distEnc -= calcAlfLCUDist(ep, compIdx, ctu, ctuYPos, ctuXPos, ctuHeight, ctuWidth, isAboveAvail, org, i_org_plane, pDec, stride_in, formatShift);
            }
            lbac_t alf_sbac_off;
            lbac_copy(&alf_sbac_off, &alf_sbac);

            // ALF ON
            rateEnc = lbac_get_bits(&alf_sbac);
            lbac_enc_alf_flag(&alf_sbac, NULL, 1);
            rateEnc = lbac_get_bits(&alf_sbac) - rateEnc;
            costEnc = (double)distEnc + (compIdx == 0 ? lambda_luma : lambda_chroma) * rateEnc;

            //ALF OFF
            distOff = 0;
            rateOff = lbac_get_bits(&alf_sbac_off);
            lbac_enc_alf_flag(&alf_sbac_off, NULL, 0);
            rateOff = lbac_get_bits(&alf_sbac_off) - rateOff;
            costOff = (double)distOff + (compIdx == 0 ? lambda_luma : lambda_chroma) * rateOff;

            if (costEnc >= costOff) {
                Enc_ALF->m_AlfLCUEnabled[ctu][compIdx] = FALSE;

                if (!isRDOEstimate) {
                    copyOneAlfBlk(pRest, pDec, stride_in, ctuYPos >> formatShift, ctuXPos >> formatShift, ctuHeight >> formatShift, ctuWidth >> formatShift, isAboveAvail, isBelowAvail);
                }
                lbac_copy(&alf_sbac, &alf_sbac_off);
            } else {
                Enc_ALF->m_AlfLCUEnabled[ctu][compIdx] = TRUE;
            }

            rateBestPic[compIdx] += (Enc_ALF->m_AlfLCUEnabled[ctu][compIdx] ? rateEnc : rateOff);
            distBestPic[compIdx] += (Enc_ALF->m_AlfLCUEnabled[ctu][compIdx] ? distEnc : distOff);
        } //CTB
    } //CTU

    for (compIdx = 0; compIdx < N_C; compIdx++) {
        if (alfPictureParam[compIdx].alf_flag == 1) {
            costAlfOn = (double)distBestPic[compIdx] + (compIdx == 0 ? lambda_luma : lambda_chroma) * (rateBestPic[compIdx] +
                        (double)(ALFParamBitrateEstimate(&alfPictureParam[compIdx])));
            costAlfOff = 0;
            if (costAlfOn >= costAlfOff) {
                alfPictureParam[compIdx].alf_flag = 0;
                for (n = 0; n < NumCUsInFrame; n++) {
                    Enc_ALF->m_AlfLCUEnabled[n][compIdx] = FALSE;
                }
                if (!isRDOEstimate) {
                    formatShift = (compIdx == Y_C) ? 0 : 1;
                    pDec = (compIdx == Y_C) ? imgY_Dec : imgUV_Dec[compIdx - U_C];
                    pRest = (compIdx == Y_C) ? imgY_Res : imgUV_Res[compIdx - U_C];
                    stride_in = (compIdx == Y_C) ? (stride) : (stride >> 1);
                    copyToImage(pRest, pDec, stride_in, img_height, img_width, formatShift);
                }
            }
        }
    }

    bestCost = 0;
    for (compIdx = 0; compIdx < N_C; compIdx++) {
        if (alfPictureParam[compIdx].alf_flag == 1) {
            bestCost += (double)distBestPic[compIdx] + (compIdx == 0 ? lambda_luma : lambda_chroma) * (rateBestPic[compIdx]);
        }
    }
    //return the block-level RD cost
    return bestCost;
}

/*
*************************************************************************
* Function: ALF filter on CTB
*************************************************************************
*/




void ADD_AlfCorrData(enc_alf_corr_t *A, enc_alf_corr_t *B, enc_alf_corr_t *C)
{
    int numCoef = ALF_MAX_NUM_COEF;
    int maxNumGroups = NO_VAR_BINS;
    int numGroups;
    int g, j, i;
    if (A->componentID >= 0) {
        numGroups = (A->componentID == Y_C) ? (maxNumGroups) : (1);
        for (g = 0; g < numGroups; g++) {
            C->pixAcc[g] = A->pixAcc[g] + B->pixAcc[g];
            for (j = 0; j < numCoef; j++) {
                C->yCorr[g][j] = A->yCorr[g][j] + B->yCorr[g][j];
                for (i = 0; i < numCoef; i++) {
                    C->ECorr[g][j][i] = A->ECorr[g][j][i] + B->ECorr[g][j][i];
                }
            }
        }
    }
}
void accumulateLCUCorrelations(enc_pic_t *ep, enc_alf_corr_t *alfCorrAcc, enc_alf_corr_t (*alfCorSrcLCU)[N_C], BOOL useAllLCUs)
{
    enc_alf_var_t *Enc_ALF = &ep->Enc_ALF;
    int compIdx, numStatLCU, addr;
    enc_alf_corr_t *alfCorrAccComp;
    int lcuHeight, lcuWidth, img_height, img_width;
    int NumCUsInFrame, numLCUInPicWidth, numLCUInPicHeight;
    lcuHeight = 1 << ep->info.log2_max_cuwh;
    lcuWidth = lcuHeight;
    img_height = ep->info.pic_height;
    img_width = ep->info.pic_width;
    numLCUInPicWidth = img_width / lcuWidth;
    numLCUInPicHeight = img_height / lcuHeight;
    numLCUInPicWidth += (img_width % lcuWidth) ? 1 : 0;
    numLCUInPicHeight += (img_height % lcuHeight) ? 1 : 0;
    NumCUsInFrame = numLCUInPicHeight * numLCUInPicWidth;
    for (compIdx = 0; compIdx < N_C; compIdx++) {
        alfCorrAccComp = &alfCorrAcc[compIdx];
        memset(alfCorrAccComp, 0, sizeof(enc_alf_corr_t));
   
        if (!useAllLCUs) {
            numStatLCU = 0;
            for (addr = 0; addr < NumCUsInFrame; addr++) {
                if (Enc_ALF->m_AlfLCUEnabled[addr][compIdx]) {
                    numStatLCU++;
                    break;
                }
            }
            if (numStatLCU == 0) {
                useAllLCUs = TRUE;
            }
        }
        for (addr = 0; addr < (int)NumCUsInFrame; addr++) {
            if (useAllLCUs || Enc_ALF->m_AlfLCUEnabled[addr][compIdx]) {
                ADD_AlfCorrData(&alfCorSrcLCU[addr][compIdx], alfCorrAccComp, alfCorrAccComp);
            }
        }
    }
}



void xcodeFiltCoeff(int *varIndTab, int numFilters, com_alf_pic_param_t *alfParam)
{
    int filterPattern[NO_VAR_BINS], i;
    memset(filterPattern, 0, NO_VAR_BINS * sizeof(int));
    alfParam->filters_per_group = numFilters;

    if (alfParam->filters_per_group > 1) {
        for (i = 1; i < NO_VAR_BINS; ++i) {
            if (varIndTab[i] != varIndTab[i - 1]) {
                filterPattern[i] = 1;
            }
        }
    }
    memcpy(alfParam->filterPattern, filterPattern, NO_VAR_BINS * sizeof(int));
    predictALFCoeff(alfParam->coeffmulti, ALF_MAX_NUM_COEF, alfParam->filters_per_group);
}

static tab_u8 tbl_weights_shape_1sym[ALF_MAX_NUM_COEF + 1] = {
    2,
    2,
    2, 2, 2,
    2, 2, 2, 1,
    1
};

void xFilterCoefQuickSort(double *coef_data, int *coef_num, int upper, int lower)
{
    double mid, tmp_data;
    int i, j, tmp_num;
    i = upper;
    j = lower;
    mid = coef_data[(lower + upper) >> 1];
    do {
        while (coef_data[i] < mid) {
            i++;
        }
        while (mid < coef_data[j]) {
            j--;
        }
        if (i <= j) {
            tmp_data = coef_data[i];
            tmp_num = coef_num[i];
            coef_data[i] = coef_data[j];
            coef_num[i] = coef_num[j];
            coef_data[j] = tmp_data;
            coef_num[j] = tmp_num;
            i++;
            j--;
        }
    } while (i <= j);
    if (upper < j) {
        xFilterCoefQuickSort(coef_data, coef_num, upper, j);
    }
    if (i < lower) {
        xFilterCoefQuickSort(coef_data, coef_num, i, lower);
    }
}


void xQuantFilterCoef(double *h, int *qh)
{
    int i, N;
    int max_value, min_value;
    double dbl_total_gain;
    int total_gain, q_total_gain;
    int upper, lower;
    double *dh;
    int  *nc;
    tab_u8 *pFiltMag;
    N = (int)ALF_MAX_NUM_COEF;
    pFiltMag = tbl_weights_shape_1sym;
    dh = (double *)malloc(N * sizeof(double));
    nc = (int *)malloc(N * sizeof(int));
    max_value = (1 << (1 + ALF_NUM_BIT_SHIFT)) - 1;
    min_value = 0 - (1 << (1 + ALF_NUM_BIT_SHIFT));
    dbl_total_gain = 0.0;
    q_total_gain = 0;
    for (i = 0; i < N; i++) {
        if (h[i] >= 0.0) {
            qh[i] = (int)(h[i] * (1 << ALF_NUM_BIT_SHIFT) + 0.5);
        } else {
            qh[i] = -(int)(-h[i] * (1 << ALF_NUM_BIT_SHIFT) + 0.5);
        }
        dh[i] = (double)qh[i] / (double)(1 << ALF_NUM_BIT_SHIFT) - h[i];
        dh[i] *= pFiltMag[i];
        dbl_total_gain += h[i] * pFiltMag[i];
        q_total_gain += qh[i] * pFiltMag[i];
        nc[i] = i;
    }
    // modification of quantized filter coefficients
    total_gain = (int)(dbl_total_gain * (1 << ALF_NUM_BIT_SHIFT) + 0.5);
    if (q_total_gain != total_gain) {
        xFilterCoefQuickSort(dh, nc, 0, N - 1);
        if (q_total_gain > total_gain) {
            upper = N - 1;
            while (q_total_gain > total_gain + 1) {
                i = nc[upper % N];
                qh[i]--;
                q_total_gain -= pFiltMag[i];
                upper--;
            }
            if (q_total_gain == total_gain + 1) {
                if (dh[N - 1] > 0) {
                    qh[N - 1]--;
                } else {
                    i = nc[upper % N];
                    qh[i]--;
                    qh[N - 1]++;
                }
            }
        } else if (q_total_gain < total_gain) {
            lower = 0;
            while (q_total_gain < total_gain - 1) {
                i = nc[lower % N];
                qh[i]++;
                q_total_gain += pFiltMag[i];
                lower++;
            }
            if (q_total_gain == total_gain - 1) {
                if (dh[N - 1] < 0) {
                    qh[N - 1]++;
                } else {
                    i = nc[lower % N];
                    qh[i]++;
                    qh[N - 1]--;
                }
            }
        }
    }
    // set of filter coefficients
    for (i = 0; i < N; i++) {
        qh[i] = COM_MAX(min_value, COM_MIN(max_value, qh[i]));
    }
    com_alf_check_coef(qh, N);
    free(dh);
    dh = NULL;
    free(nc);
    nc = NULL;
}


double xfindBestCoeffCodMethod(int(*filterCoeffSymQuant)[ALF_MAX_NUM_COEF], int sqrFiltLength, int filters_per_fr,
                               double errorForce0CoeffTab[NO_VAR_BINS][2], double lambda)
{
    int coeffBits, i;
    double error = 0, lagrangian;
    int coeffmulti[NO_VAR_BINS][ALF_MAX_NUM_COEF];
    int g;

    for (g = 0; g < filters_per_fr; g++) {
        for (i = 0; i < sqrFiltLength; i++) {
            coeffmulti[g][i] = filterCoeffSymQuant[g][i];
        }
    }
    predictALFCoeff(coeffmulti, sqrFiltLength, filters_per_fr);
    coeffBits = 0;
    for (g = 0; g < filters_per_fr; g++) {
        coeffBits += filterCoeffBitrateEstimate(coeffmulti[g]);
    }
    for (i = 0; i < filters_per_fr; i++) {
        error += errorForce0CoeffTab[i][1];
    }
    lagrangian = error + lambda * coeffBits;

    return (lagrangian);
}

int gnsCholeskyDec(double (*inpMatr)[ALF_MAX_NUM_COEF], double outMatr[ALF_MAX_NUM_COEF][ALF_MAX_NUM_COEF], int noEq)
{
    int i, j, k;     /* Looping Variables */
    double scale;       /* scaling factor for each row */
    double invDiag[ALF_MAX_NUM_COEF];  /* Vector of the inverse of diagonal entries of outMatr */
    //  Cholesky decomposition starts
    for (i = 0; i < noEq; i++) {
        for (j = i; j < noEq; j++) {
            /* Compute the scaling factor */
            scale = inpMatr[i][j];
            if (i > 0) {
                for (k = i - 1; k >= 0; k--) {
                    scale -= outMatr[k][j] * outMatr[k][i];
                }
            }
            /* Compute i'th row of outMatr */
            if (i == j) {
                if (scale <= REG_SQR) {  // if(scale <= 0 )  /* If inpMatr is singular */
                    return 0;
                }
                else {
                    /* Normal operation */
                    invDiag[i] = 1.0 / (outMatr[i][i] = sqrt(scale));
                }
            }
            else {
                outMatr[i][j] = scale * invDiag[i]; /* Upper triangular part          */
                outMatr[j][i] = 0.0;              /* Lower triangular part set to 0 */
            }
        }
    }
    return 1; /* Signal that Cholesky factorization is successfully performed */
}

void gnsTransposeBacksubstitution(double U[ALF_MAX_NUM_COEF][ALF_MAX_NUM_COEF], double rhs[], double x[], int order)
{
    int i, j;             /* Looping variables */
    double sum;              /* Holds backsubstitution from already handled rows */
    /* Backsubstitution starts */
    x[0] = rhs[0] / U[0][0];               /* First row of U'                   */
    for (i = 1; i < order; i++) {
        /* For the rows 1..order-1           */
        for (j = 0, sum = 0.0; j < i; j++) { /* Backsubst already solved unknowns */
            sum += x[j] * U[j][i];
        }
        x[i] = (rhs[i] - sum) / U[i][i];       /* i'th component of solution vect.  */
    }
}
void gnsBacksubstitution(double R[ALF_MAX_NUM_COEF][ALF_MAX_NUM_COEF], double z[ALF_MAX_NUM_COEF], int R_size,
    double A[ALF_MAX_NUM_COEF])
{
    int i, j;
    double sum;
    R_size--;
    A[R_size] = z[R_size] / R[R_size][R_size];
    for (i = R_size - 1; i >= 0; i--) {
        for (j = i + 1, sum = 0.0; j <= R_size; j++) {
            sum += R[i][j] * A[j];
        }
        A[i] = (z[i] - sum) / R[i][i];
    }
}


int gnsSolveByChol(double(*LHS)[ALF_MAX_NUM_COEF], double *rhs, double *x, int noEq)
{
    double aux[ALF_MAX_NUM_COEF];     /* Auxiliary vector */
    double U[ALF_MAX_NUM_COEF][ALF_MAX_NUM_COEF];    /* Upper triangular Cholesky factor of LHS */
    int  i, singular;          /* Looping variable */
    assert(noEq > 0);
    /* The equation to be solved is LHSx = rhs */
    /* Compute upper triangular U such that U'*U = LHS */
    if (gnsCholeskyDec(LHS, U, noEq)) { /* If Cholesky decomposition has been successful */
        singular = 1;
        /* Now, the equation is  U'*U*x = rhs, where U is upper triangular
        * Solve U'*aux = rhs for aux
        */
        gnsTransposeBacksubstitution(U, rhs, aux, noEq);
        /* The equation is now U*x = aux, solve it for x (new motion coefficients) */
        gnsBacksubstitution(U, aux, noEq, x);
    }
    else { /* LHS was singular */
        singular = 0;
        /* Regularize LHS */
        for (i = 0; i < noEq; i++) {
            LHS[i][i] += REG;
        }
        /* Compute upper triangular U such that U'*U = regularized LHS */
        singular = gnsCholeskyDec(LHS, U, noEq);
        if (singular == 1) {
            /* Solve  U'*aux = rhs for aux */
            gnsTransposeBacksubstitution(U, rhs, aux, noEq);
            /* Solve U*x = aux for x */
            gnsBacksubstitution(U, aux, noEq, x);
        }
        else {
            x[0] = 1.0;
            for (i = 1; i < noEq; i++) {
                x[i] = 0.0;
            }
        }
    }
    return singular;
}

double calculateErrorAbs(double(*A)[ALF_MAX_NUM_COEF], double *b, double y, int size)
{
    int i;
    double error, sum;
    double c[ALF_MAX_NUM_COEF];
    gnsSolveByChol(A, b, c, size);
    sum = 0;
    for (i = 0; i < size; i++) {
        sum += c[i] * b[i];
    }
    error = y - sum;
    return error;
}

double mergeFiltersGreedy(enc_alf_var_t *Enc_ALF, double (*yGlobalSeq)[ALF_MAX_NUM_COEF], double (*EGlobalSeq)[ALF_MAX_NUM_COEF][ALF_MAX_NUM_COEF], double *pixAccGlobalSeq,
                          int intervalBest[NO_VAR_BINS][2],
                          double error_tab[NO_VAR_BINS], double error_comb_tab[NO_VAR_BINS],
                          int indexList[NO_VAR_BINS], int available[NO_VAR_BINS],
                          int sqrFiltLength, int noIntervals)
{
    int first, ind, ind1, ind2, i, j, bestToMerge;
    double error, error1, error2, errorMin;

    if (noIntervals == NO_VAR_BINS) {
        for (ind = 0; ind < NO_VAR_BINS; ind++) {
            indexList[ind] = ind;
            available[ind] = 1;
            Enc_ALF->m_pixAcc_merged[ind] = pixAccGlobalSeq[ind];
            memcpy(Enc_ALF->m_y_merged[ind], yGlobalSeq[ind], sizeof(double)*sqrFiltLength);
            for (i = 0; i < sqrFiltLength; i++) {
                memcpy(Enc_ALF->m_E_merged[ind][i], EGlobalSeq[ind][i], sizeof(double)*sqrFiltLength);
            }
        }
        for (ind = 0; ind < NO_VAR_BINS; ind++) {
            error_tab[ind] = calculateErrorAbs(Enc_ALF->m_E_merged[ind], Enc_ALF->m_y_merged[ind], Enc_ALF->m_pixAcc_merged[ind],
                                               sqrFiltLength);
        }
        for (ind = 0; ind < NO_VAR_BINS - 1; ind++) {
            ind1 = indexList[ind];
            ind2 = indexList[ind + 1];
            error1 = error_tab[ind1];
            error2 = error_tab[ind2];
            double pixAcc_temp = Enc_ALF->m_pixAcc_merged[ind1] + Enc_ALF->m_pixAcc_merged[ind2];
            for (i = 0; i < sqrFiltLength; i++) {
                Enc_ALF->m_y_temp[i] = Enc_ALF->m_y_merged[ind1][i] + Enc_ALF->m_y_merged[ind2][i];
                for (j = 0; j < sqrFiltLength; j++) {
                    Enc_ALF->m_E_temp[i][j] = Enc_ALF->m_E_merged[ind1][i][j] + Enc_ALF->m_E_merged[ind2][i][j];
                }
            }
            error_comb_tab[ind1] = calculateErrorAbs(Enc_ALF->m_E_temp, Enc_ALF->m_y_temp, pixAcc_temp,
                                   sqrFiltLength) - error1 - error2;
        }
    } else {
        errorMin = 0;
        first = 1;
        bestToMerge = 0;
        for (ind = 0; ind < noIntervals; ind++) {
            error = error_comb_tab[indexList[ind]];
            if ((error < errorMin || first == 1)) {
                errorMin = error;
                bestToMerge = ind;
                first = 0;
            }
        }
        ind1 = indexList[bestToMerge];
        ind2 = indexList[bestToMerge + 1];
        Enc_ALF->m_pixAcc_merged[ind1] += Enc_ALF->m_pixAcc_merged[ind2];
        for (i = 0; i < sqrFiltLength; i++) {
            Enc_ALF->m_y_merged[ind1][i] += Enc_ALF->m_y_merged[ind2][i];
            for (j = 0; j < sqrFiltLength; j++) {
                Enc_ALF->m_E_merged[ind1][i][j] += Enc_ALF->m_E_merged[ind2][i][j];
            }
        }
        available[ind2] = 0;
        //update error tables
        error_tab[ind1] = error_comb_tab[ind1] + error_tab[ind1] + error_tab[ind2];
        if (indexList[bestToMerge] > 0) {
            ind1 = indexList[bestToMerge - 1];
            ind2 = indexList[bestToMerge];
            error1 = error_tab[ind1];
            error2 = error_tab[ind2];
            double pixAcc_temp = Enc_ALF->m_pixAcc_merged[ind1] + Enc_ALF->m_pixAcc_merged[ind2];
            for (i = 0; i < sqrFiltLength; i++) {
                Enc_ALF->m_y_temp[i] = Enc_ALF->m_y_merged[ind1][i] + Enc_ALF->m_y_merged[ind2][i];
                for (j = 0; j < sqrFiltLength; j++) {
                    Enc_ALF->m_E_temp[i][j] = Enc_ALF->m_E_merged[ind1][i][j] + Enc_ALF->m_E_merged[ind2][i][j];
                }
            }
            error_comb_tab[ind1] = calculateErrorAbs(Enc_ALF->m_E_temp, Enc_ALF->m_y_temp, pixAcc_temp,
                                   sqrFiltLength) - error1 - error2;
        }
        if (indexList[bestToMerge + 1] < NO_VAR_BINS - 1) {
            ind1 = indexList[bestToMerge];
            ind2 = indexList[bestToMerge + 2];
            error1 = error_tab[ind1];
            error2 = error_tab[ind2];
            double pixAcc_temp = Enc_ALF->m_pixAcc_merged[ind1] + Enc_ALF->m_pixAcc_merged[ind2];
            for (i = 0; i < sqrFiltLength; i++) {
                Enc_ALF->m_y_temp[i] = Enc_ALF->m_y_merged[ind1][i] + Enc_ALF->m_y_merged[ind2][i];
                for (j = 0; j < sqrFiltLength; j++) {
                    Enc_ALF->m_E_temp[i][j] = Enc_ALF->m_E_merged[ind1][i][j] + Enc_ALF->m_E_merged[ind2][i][j];
                }
            }
            error_comb_tab[ind1] = calculateErrorAbs(Enc_ALF->m_E_temp, Enc_ALF->m_y_temp, pixAcc_temp,
                                   sqrFiltLength) - error1 - error2;
        }
        ind = 0;
        for (i = 0; i < NO_VAR_BINS; i++) {
            if (available[i] == 1) {
                indexList[ind] = i;
                ind++;
            }
        }
    }
    errorMin = 0;
    for (ind = 0; ind < noIntervals; ind++) {
        errorMin += error_tab[indexList[ind]];
    }
    for (ind = 0; ind < noIntervals - 1; ind++) {
        intervalBest[ind][0] = indexList[ind];
        intervalBest[ind][1] = indexList[ind + 1] - 1;
    }
    intervalBest[noIntervals - 1][0] = indexList[noIntervals - 1];
    intervalBest[noIntervals - 1][1] = NO_VAR_BINS - 1;
    return (errorMin);
}

void add_A(double (*Amerged)[ALF_MAX_NUM_COEF], double (*A)[ALF_MAX_NUM_COEF][ALF_MAX_NUM_COEF], int start, int stop, int size)
{
    int i, j, ind;          /* Looping variable */
    for (i = 0; i < size; i++) {
        for (j = 0; j < size; j++) {
            Amerged[i][j] = 0;
            for (ind = start; ind <= stop; ind++) {
                Amerged[i][j] += A[ind][i][j];
            }
        }
    }
}

void add_b(double *bmerged, double (*b)[ALF_MAX_NUM_COEF], int start, int stop, int size)
{
    int i, ind;          /* Looping variable */
    for (i = 0; i < size; i++) {
        bmerged[i] = 0;
        for (ind = start; ind <= stop; ind++) {
            bmerged[i] += b[ind][i];
        }
    }
}

void roundFiltCoeff(int *FilterCoeffQuan, double *FilterCoeff, int sqrFiltLength, int factor)
{
    int i;
    double diff;
    int diffInt, sign;
    for (i = 0; i < sqrFiltLength; i++) {
        sign = (FilterCoeff[i] > 0) ? 1 : -1;
        diff = FilterCoeff[i] * sign;
        diffInt = (int)(diff * (double)factor + 0.5);
        FilterCoeffQuan[i] = diffInt * sign;
    }
}

double calculateErrorCoeffProvided(double (*A)[ALF_MAX_NUM_COEF], double *b, double *c, int size)
{
    int i, j;
    double error, sum = 0;
    error = 0;
    for (i = 0; i < size; i++) { //diagonal
        sum = 0;
        for (j = i + 1; j < size; j++) {
            sum += (A[j][i] + A[i][j]) * c[j];
        }
        error += (A[i][i] * c[i] + sum - 2 * b[i]) * c[i];
    }
    return error;
}

static double alf_quant_int_flt_coef(double *filterCoeff, int *filterCoeffQuant, double (*E)[ALF_MAX_NUM_COEF], double *y, int sqrFiltLength)
{
    double error;
    int filterCoeffQuantMod[ALF_MAX_NUM_COEF];
    int factor = (1 << ((int)ALF_NUM_BIT_SHIFT));
    int i;
    int quantCoeffSum, minInd, targetCoeffSumInt, k, diff;
    double targetCoeffSum, errMin;

    gnsSolveByChol(E, y, filterCoeff, sqrFiltLength);
    targetCoeffSum = 0;
    for (i = 0; i < sqrFiltLength; i++) {
        targetCoeffSum += (tbl_weights_shape_1sym[i] * filterCoeff[i] * factor);
    }
    targetCoeffSumInt = ROUND(targetCoeffSum);
    roundFiltCoeff(filterCoeffQuant, filterCoeff, sqrFiltLength, factor);
    quantCoeffSum = 0;
    for (i = 0; i < sqrFiltLength; i++) {
        quantCoeffSum += tbl_weights_shape_1sym[i] * filterCoeffQuant[i];
    }
    while (quantCoeffSum != targetCoeffSumInt) {
        if (quantCoeffSum > targetCoeffSumInt) {
            diff = quantCoeffSum - targetCoeffSumInt;
            errMin = 0;
            minInd = -1;
            for (k = 0; k < sqrFiltLength; k++) {
                if (tbl_weights_shape_1sym[k] <= diff) {
                    for (i = 0; i < sqrFiltLength; i++) {
                        filterCoeffQuantMod[i] = filterCoeffQuant[i];
                    }
                    filterCoeffQuantMod[k]--;
                    for (i = 0; i < sqrFiltLength; i++) {
                        filterCoeff[i] = (double)filterCoeffQuantMod[i] / (double)factor;
                    }
                    error = calculateErrorCoeffProvided(E, y, filterCoeff, sqrFiltLength);
                    if (error < errMin || minInd == -1) {
                        errMin = error;
                        minInd = k;
                    }
                } // if (weights(k)<=diff)
            } // for (k=0; k<sqrFiltLength; k++)
            filterCoeffQuant[minInd]--;
        } else {
            diff = targetCoeffSumInt - quantCoeffSum;
            errMin = 0;
            minInd = -1;
            for (k = 0; k < sqrFiltLength; k++) {
                if (tbl_weights_shape_1sym[k] <= diff) {
                    for (i = 0; i < sqrFiltLength; i++) {
                        filterCoeffQuantMod[i] = filterCoeffQuant[i];
                    }
                    filterCoeffQuantMod[k]++;
                    for (i = 0; i < sqrFiltLength; i++) {
                        filterCoeff[i] = (double)filterCoeffQuantMod[i] / (double)factor;
                    }
                    error = calculateErrorCoeffProvided(E, y, filterCoeff, sqrFiltLength);
                    if (error < errMin || minInd == -1) {
                        errMin = error;
                        minInd = k;
                    }
                } // if (weights(k)<=diff)
            } // for (k=0; k<sqrFiltLength; k++)
            filterCoeffQuant[minInd]++;
        }
        quantCoeffSum = 0;
        for (i = 0; i < sqrFiltLength; i++) {
            quantCoeffSum += tbl_weights_shape_1sym[i] * filterCoeffQuant[i];
        }
    }
    com_alf_check_coef(filterCoeffQuant, sqrFiltLength);
    for (i = 0; i < sqrFiltLength; i++) {
        filterCoeff[i] = (double)filterCoeffQuant[i] / (double)factor;
    }
    error = calculateErrorCoeffProvided(E, y, filterCoeff, sqrFiltLength);

    return (error);
}

static double alf_find_coef(enc_alf_var_t *Enc_ALF, double(*EGlobalSeq)[ALF_MAX_NUM_COEF][ALF_MAX_NUM_COEF], double(*yGlobalSeq)[ALF_MAX_NUM_COEF], double *pixAccGlobalSeq, int(*filterCoeffSeq)[ALF_MAX_NUM_COEF],
    int(*filterCoeffQuantSeq)[ALF_MAX_NUM_COEF], int intervalBest[NO_VAR_BINS][2], int varIndTab[NO_VAR_BINS], int sqrFiltLength,
    int filters_per_fr, double errorTabForce0Coeff[NO_VAR_BINS][2])
{
    int filterCoeffQuant[ALF_MAX_NUM_COEF];
    double filterCoeff[ALF_MAX_NUM_COEF];
    double error;
    int k, filtNo;

    error = 0;
    for (filtNo = 0; filtNo < filters_per_fr; filtNo++) {
        add_A(Enc_ALF->m_E_temp, EGlobalSeq, intervalBest[filtNo][0], intervalBest[filtNo][1], sqrFiltLength);
        add_b(Enc_ALF->m_y_temp, yGlobalSeq, intervalBest[filtNo][0], intervalBest[filtNo][1], sqrFiltLength);
        double pixAcc_temp = 0;
        for (k = intervalBest[filtNo][0]; k <= intervalBest[filtNo][1]; k++) {
            pixAcc_temp += pixAccGlobalSeq[k];
        }
        // Find coeffcients
        errorTabForce0Coeff[filtNo][1] = pixAcc_temp + alf_quant_int_flt_coef(filterCoeff, filterCoeffQuant, Enc_ALF->m_E_temp,
            Enc_ALF->m_y_temp, sqrFiltLength);
        errorTabForce0Coeff[filtNo][0] = pixAcc_temp;
        error += errorTabForce0Coeff[filtNo][1];
        for (k = 0; k < sqrFiltLength; k++) {
            filterCoeffSeq[filtNo][k] = filterCoeffQuant[k];
            filterCoeffQuantSeq[filtNo][k] = filterCoeffQuant[k];
        }
    }
    for (filtNo = 0; filtNo < filters_per_fr; filtNo++) {
        for (k = intervalBest[filtNo][0]; k <= intervalBest[filtNo][1]; k++) {
            varIndTab[k] = filtNo;
        }
    }
    return (error);
}

static void alf_find_best_flt_var(enc_alf_var_t *Enc_ALF, double(*ySym)[ALF_MAX_NUM_COEF], double(*ESym)[ALF_MAX_NUM_COEF][ALF_MAX_NUM_COEF], double *pixAcc, int(*filterCoeffSym)[ALF_MAX_NUM_COEF],
    int *filters_per_fr_best, int varIndTab[], double lambda_val, int numMaxFilters)
{
    int filterCoeffSymQuant[NO_VAR_BINS][ALF_MAX_NUM_COEF];
    int filters_per_fr, firstFilt, interval[NO_VAR_BINS][2], intervalBest[NO_VAR_BINS][2];
    double  lagrangian, lagrangianMin;
    int sqrFiltLength;
    double errorForce0CoeffTab[NO_VAR_BINS][2];
    double error_tab[NO_VAR_BINS], error_comb_tab[NO_VAR_BINS];
    int indexList[NO_VAR_BINS], available[NO_VAR_BINS];

    sqrFiltLength = (int)ALF_MAX_NUM_COEF;

    // zero all variables
    memset(varIndTab, 0, sizeof(int)*NO_VAR_BINS);
    for (int i = 0; i < NO_VAR_BINS; i++) {
        memset(filterCoeffSym[i], 0, sizeof(int)*ALF_MAX_NUM_COEF);
        memset(filterCoeffSymQuant[i], 0, sizeof(int)*ALF_MAX_NUM_COEF);
    }
    firstFilt = 1;
    lagrangianMin = 0;
    filters_per_fr = NO_VAR_BINS;
    while (filters_per_fr >= 1) {
        mergeFiltersGreedy(Enc_ALF, ySym, ESym, pixAcc, interval, error_tab, error_comb_tab, indexList, available, sqrFiltLength, filters_per_fr);

        alf_find_coef(Enc_ALF, ESym, ySym, pixAcc, filterCoeffSym, filterCoeffSymQuant, interval,
            varIndTab, sqrFiltLength, filters_per_fr, errorForce0CoeffTab);
        lagrangian = xfindBestCoeffCodMethod(filterCoeffSymQuant, sqrFiltLength, filters_per_fr,
            errorForce0CoeffTab, lambda_val);
        if (lagrangian < lagrangianMin || firstFilt == 1 || filters_per_fr == numMaxFilters) {
            firstFilt = 0;
            lagrangianMin = lagrangian;
            (*filters_per_fr_best) = filters_per_fr;
            memcpy(intervalBest, interval, NO_VAR_BINS * 2 * sizeof(int));
        }
        filters_per_fr--;
    }
    alf_find_coef(Enc_ALF, ESym, ySym, pixAcc, filterCoeffSym, filterCoeffSymQuant, intervalBest,
        varIndTab, sqrFiltLength, (*filters_per_fr_best), errorForce0CoeffTab);
    if (*filters_per_fr_best == 1) {
        memset(varIndTab, 0, sizeof(int)*NO_VAR_BINS);
    }
}

static void alf_derive_flt_coef(enc_alf_var_t *Enc_ALF, int compIdx, enc_alf_corr_t *alfCorr, com_alf_pic_param_t *alfFiltParam, int maxNumFilters, double lambda)
{
    int numCoeff = ALF_MAX_NUM_COEF;
    double coef[ALF_MAX_NUM_COEF];
    int lambdaForMerge, numFilters;
    switch (compIdx) {
    case Y_C: {
        lambdaForMerge = ((int)lambda) * (1 << (2 * Enc_ALF->m_uiBitIncrement));
        memset(Enc_ALF->m_varIndTab, 0, sizeof(int)*NO_VAR_BINS);
        alf_find_best_flt_var(Enc_ALF, alfCorr->yCorr, alfCorr->ECorr, alfCorr->pixAcc, alfFiltParam->coeffmulti, &numFilters,
            Enc_ALF->m_varIndTab, lambdaForMerge, maxNumFilters);
        xcodeFiltCoeff(Enc_ALF->m_varIndTab, numFilters, alfFiltParam);
    }
              break;
    case U_C:
    case V_C: {
        alfFiltParam->filters_per_group = 1;
        gnsSolveByChol(alfCorr->ECorr[0], alfCorr->yCorr[0], coef, numCoeff);
        xQuantFilterCoef(coef, alfFiltParam->coeffmulti[0]);
        predictALFCoeff(alfFiltParam->coeffmulti, numCoeff, alfFiltParam->filters_per_group);
    }
              break;
    default: {
        printf("Not a legal component ID\n");
        assert(0);
        exit(-1);
    }
    }
}

static void alf_decide_pic_param(enc_alf_var_t *Enc_ALF, com_alf_pic_param_t *alfPictureParam, enc_alf_corr_t *alfCorr, double lambdaLuma)
{
    double lambdaWeighting = (1.0);
    int compIdx;
    double lambda;
    com_alf_pic_param_t *alfParam;
    enc_alf_corr_t *picCorr;
    for (compIdx = 0; compIdx < N_C; compIdx++) {
        //VKTBD chroma need different lambdas? lambdaWeighting needed?
        lambda = lambdaLuma * lambdaWeighting;
        alfParam = &alfPictureParam[compIdx];
        picCorr = &alfCorr[compIdx];
        alfParam->alf_flag = 1;
        alf_derive_flt_coef(Enc_ALF, compIdx, picCorr, alfParam, NO_VAR_BINS, lambda);
    }
}

static void alf_get_filter_coefs(enc_pic_t *ep, lbac_t *lbac, com_alf_pic_param_t *alfPictureParam, double lambda)
{
    enc_alf_var_t *Enc_ALF = &ep->Enc_ALF;
    int compIdx, i;
    enc_alf_corr_t alfPicCorr[N_C];
    double costMin, cost;
    com_alf_pic_param_t tempAlfParam[N_C];
    int picHeaderBitrate = 0;
    for (compIdx = 0; compIdx < N_C; compIdx++) {
        tempAlfParam[compIdx].alf_flag = 0;
        tempAlfParam[compIdx].filters_per_group = 1;
        tempAlfParam[compIdx].componentID = compIdx;
    }
    memset(alfPicCorr, 0, sizeof(alfPicCorr));

    // normal part
    costMin = 1.7e+308; // max double

    accumulateLCUCorrelations(ep, alfPicCorr, Enc_ALF->m_alfCorr, TRUE);
    alf_decide_pic_param(Enc_ALF, tempAlfParam, alfPicCorr, lambda);

    for (i = 0; i < ALF_REDESIGN_ITERATION; i++) {
        if (i != 0) {
            //redesign filter according to the last on off results
            accumulateLCUCorrelations(ep, alfPicCorr, Enc_ALF->m_alfCorr, FALSE);
            alf_decide_pic_param(Enc_ALF, tempAlfParam, alfPicCorr, lambda);
        }
        //estimate cost
        cost = alf_decision_all_lcu(ep, lbac, tempAlfParam, lambda, TRUE, Enc_ALF->m_alfCorr, NULL, NULL, 0, NULL, NULL, NULL, NULL, 0);
        picHeaderBitrate = estimateALFBitrateInPicHeader(tempAlfParam);
        cost += (double)picHeaderBitrate * lambda;

        if (cost < costMin) {
            costMin = cost;
            for (compIdx = 0; compIdx < N_C; compIdx++) {
                com_alf_copy_param(&alfPictureParam[compIdx], &tempAlfParam[compIdx]);
            }
        }
    }
}

static void alf_decision(enc_pic_t *ep, com_alf_pic_param_t *alfPictureParam, com_pic_t *pic_rec, com_pic_t *pic_alf_Org, com_pic_t *pic_alf_Rec, double lambda_mode)
{
    int lumaStride; // Add to avoid micro trubles
    pel *pResY, *pDecY, *pOrgY;
    pel *pResUV[2], *pDecUV[2], *pOrgUV[2];
    lbac_t sbac_alf;
    lbac_t *lbac = &sbac_alf;

    pResY = pic_rec->y;
    pResUV[0] = pic_rec->u;
    pResUV[1] = pic_rec->v;
    pDecY = pic_alf_Rec->y;
    pDecUV[0] = pic_alf_Rec->u;
    pDecUV[1] = pic_alf_Rec->v;
    pOrgY = pic_alf_Org->y;
    pOrgUV[0] = pic_alf_Org->u;
    pOrgUV[1] = pic_alf_Org->v;

    lbac_reset(lbac); // init lbac for alf rdo
    com_sbac_ctx_init(&lbac->h);

    lumaStride = pic_rec->stride_luma;

    alf_get_statistics(ep, pOrgY, pOrgUV, pic_alf_Org->stride_luma, pDecY, pDecUV, lumaStride);
    alf_get_filter_coefs(ep, lbac, alfPictureParam, lambda_mode);
    alf_decision_all_lcu(ep, lbac, alfPictureParam, lambda_mode, FALSE, NULL, pOrgY, pOrgUV, pic_alf_Org->stride_luma, pDecY, pDecUV, pResY, pResUV, lumaStride);

}

int enc_alf_avs2(enc_pic_t *ep, com_pic_t *pic_rec, com_pic_t *pic_org, double lambda)
{
    com_alf_pic_param_t *final_alfPictureParam = ep->Enc_ALF.m_alfPictureParam;
    int bit_depth = ep->info.bit_depth_internal;
    int scale_lambda = (bit_depth == 10) ? ep->info.qp_offset_bit_depth : 1;
    ep->pic_alf_Rec->stride_luma = pic_rec->stride_luma;
    ep->pic_alf_Rec->stride_chroma = pic_rec->stride_chroma;
    com_alf_copy_frm(ep->pic_alf_Rec, pic_rec);

    alf_decision(ep, final_alfPictureParam, pic_rec, pic_org, ep->pic_alf_Rec, lambda * scale_lambda);

    for (int i = 0; i < N_C; i++) {
        ep->pic_alf_on[i] = final_alfPictureParam[i].alf_flag;
    }
    return COM_OK;
}

