#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <graph.h>
#include <EasyBMP.h>
#include "basicutil.h"

#include "ncutknn.h"
#include "ncutknnmulti.h"
#include "aaknn.h"

enum Method {NCUTKNN, AAKNN, NCUTKNNMULTI};
Method method;
enum UserInput {BOX, SEEDS, FROMIMG};
UserInput userinput = BOX;
enum ErrorMeasure {ERRORRATE, FMEASURE, JACCARD, NOMEASURE};
ErrorMeasure errormeasure = ERRORRATE;

int main(int argc, char * argv[])
{
    srand( (unsigned)time( NULL ) );
    double totaltime = 0; // timing
    const char * UsageStr = "Usage: main -d DBdirectory -i imagename -m method [-o outputdirectory] [-w width_R width_G width_B] [-s w_smoothness] [-e errormeasure] [-u userinput] [-h on or off (hardconstraints)] [-b binsize] [-x xybinsize] [-k knn_k knnfiledirectory] [-g number_of_gaussian_components]\n";
    bool hardconstraintsflag = true;
    char * dbdir = NULL, * imgname = NULL, * methodstr = NULL, * outputdir = NULL;
    char * knnfiledir = NULL;
    char * initlabelingimgpath = NULL;
    Table2D<int> knntable;
    double kernelwidth[3];
    double w_smooth = 0;
    double binsize = 16.0;
    int xybinsize=0;
    int patchsize = 1;
    int num_comps = 5; // number of components for GMM
    int KNN_K;
    if(argc == 1){ 
        printf("%s",UsageStr);
        exit(-1);
    }
    for(int i=0;i<argc;i++){
        printf("%s ",argv[i]);
    }
    printf("\n");
    
    int opt;
    while((opt = getopt(argc, argv, "dimeuowshbxkpg")) != -1){
        switch(opt){
            case 'd':
                dbdir = argv[optind];
                break;
            case 'i':
                imgname = argv[optind];
                break;
            case 'm':
                methodstr = argv[optind];
                if(0 == strcmp(argv[optind],"ncutknn")){
                    method = NCUTKNN;
                }
                else if(0 == strcmp(argv[optind],"aaknn")){
                    method = AAKNN;
                }
                else if(0 == strcmp(argv[optind],"ncutknnmulti")){
                    method = NCUTKNNMULTI;
                }
                else{
                    printf("method not valid!\n");
                    exit(-1);
                }
                break;
            case 'e':
                if(0 == strcmp(argv[optind],"errorrate"))
                    errormeasure = ERRORRATE;
                else if(0 == strcmp(argv[optind],"fmeasure"))
                    errormeasure = FMEASURE;
                else if(0 == strcmp(argv[optind],"jaccard"))
                    errormeasure = JACCARD;
                else if(0 == strcmp(argv[optind],"nomeasure"))
                    errormeasure = NOMEASURE;
                else{
                    printf("measure not valid!\n");
                    exit(-1);
                }
                break;
            case 'u':
                if(0 == strcmp(argv[optind],"box"))
                    userinput = BOX;
                else if(0 == strcmp(argv[optind],"seeds"))
                    userinput = SEEDS;
                else if(0 == strcmp(argv[optind],"fromimage")){
                    userinput = FROMIMG;
                    initlabelingimgpath = argv[optind+1];
                }
                else{
                    printf("User input not valid!\n");
                    exit(-1);
                }
                break;
            case 'o':
                outputdir = argv[optind];
                break;
            case 's':
                w_smooth = atof(argv[optind]);
                break;
            case 'h':
                if(0 == strcmp(argv[optind],"on"))
                    hardconstraintsflag = true;
                else if(0 == strcmp(argv[optind],"off")){
                    hardconstraintsflag = false;
                }
                break;
            case 'w':
                kernelwidth[0] = atof(argv[optind]);
                kernelwidth[1] = atof(argv[optind + 1]);
                kernelwidth[2] = atof(argv[optind + 2]);
                break;
            case 'b':
                binsize = atof(argv[optind]);
                break;
            case 'x':
                xybinsize = atoi(argv[optind]);
                break;
            case 'p':
                patchsize = atoi(argv[optind]);
                break;
            case 'k':
                KNN_K = atoi(argv[optind]);
                knnfiledir = argv[optind+1];
                break;
            case 'g':
                num_comps = atoi(argv[optind]);
                break;
            default: /* '?' */
                fprintf(stderr, "%s", UsageStr);
                break;
        }
    }
    if((dbdir != NULL) && (imgname != NULL) && (methodstr != NULL))
        printf("database %s\nimage %s\nmethod %s\n", dbdir, imgname, methodstr);
    else
        printf("%s",UsageStr);
        
    int numimg = 0;
    if(0==strcmp(imgname,"all"))
        numimg = countFilesInDirectory((dbdir+string("/images")).c_str());
    else
        numimg = 1;

    // read directory
	DIR *dpdf;
    struct dirent *epdf;
    dpdf = opendir((dbdir+string("/images")).c_str());
    if (dpdf == NULL) {
       printf("directory %s\n",(dbdir+string("/images")).c_str()); 
       printf("directory empty!\n"); 
       exit(-1);
    }
    int imgid=0;
    double * measures = new double[numimg];
    while (epdf = readdir(dpdf)){
        char filename[20];
        strcpy(filename,epdf->d_name);
        if(strlen(filename)<=2) continue;
        char * shortname = (char *) malloc(strlen(filename)-3);
        strncpy(shortname,filename,strlen(filename)-4);
        shortname[strlen(filename)-4]='\0';

        if(strcmp(imgname,"all") != 0 && strcmp(imgname, shortname) != 0)
            continue;
        printf("image %d : %s\n",imgid,shortname);
        
        // Read the RGB image
        Image image = Image((dbdir+string("/images/")+string(shortname) + string(".bmp")).c_str(),shortname,binsize,8);
        
        int imgw = image.img_w;
        int imgh = image.img_h;
        // read subpixel images
        Table2D<Vect3D> floatimg(imgw,imgh);
        Table2D<double> column_img;
        readbinfile(column_img, (dbdir+string("/subpixelimages/")+string(shortname) + string(".bin")).c_str(), 1, imgh*imgw*3);
        int idx = 0;
        for(int c=0;c<3;c++){
            for(int i=0;i<imgw;i++){
                for(int j=0;j<imgh;j++){
                    if(c==0) floatimg[i][j].x = column_img[0][idx++];
                    if(c==1) floatimg[i][j].y = column_img[0][idx++];
                    if(c==2) floatimg[i][j].z = column_img[0][idx++];
                }
            }
        }
            
        // Initial labeling
        Table2D<Label> initlabeling(imgw,imgh,NONE);
        Table2D<Label> hardconstraints(imgw,imgh,NONE);
        if(BOX == userinput){
            initlabeling = getinitlabeling(loadImage<RGB>((dbdir+string("/boxes/")+string(shortname) + string(".bmp")).c_str()),0);
            for(int i=0;i<imgw;i++){
			    for(int j=0;j<imgh;j++){
				    if(initlabeling[i][j]==BKG) hardconstraints[i][j] = BKG;
				    else hardconstraints[i][j] = NONE;
			    }
		    }
		    //image.addboxsmooth(getROI(hardconstraints,NONE));
		}
        else if((SEEDS == userinput) && (method != NCUTKNNMULTI)){
            initlabeling = getinitlabelingFB(loadImage<RGB>((dbdir+string("/seeds/")+string(shortname) + string(".bmp")).c_str()), red, blue);
            hardconstraints = initlabeling;
        }
        else if(FROMIMG == userinput){
            Table2D<RGB> initlabelingimg = loadImage<RGB>(initlabelingimgpath);
            for(int i=0;i<imgw;i++)
	        {
		        for(int j=0;j<imgh;j++)
		        {
			        if(initlabelingimg[i][j]==white)
				        initlabeling[i][j] = BKG;
			        else
				        initlabeling[i][j] = OBJ;
		        }
	        }
            hardconstraints = initlabeling;
        }
        
        Table2D<int> initlabeling_multi(imgw,imgh,0); // for multilabel
        const int numColor = 6;
        RGB colors[numColor] = {white,red,green,blue,black,navy};
        if(method == NCUTKNNMULTI){
            if(userinput == SEEDS)
                initlabeling_multi = getinitlabelingMULTI(loadImage<RGB>((dbdir+string("/seedsmulti/")+string(shortname) + string(".bmp")).c_str()), colors, numColor);
            else if(userinput == FROMIMG)
                initlabeling_multi = getinitlabelingMULTI(loadImage<RGB>(initlabelingimgpath), colors, numColor);
        }
        
        if(hardconstraintsflag==false) hardconstraints.reset(NONE);
        
        if(method == NCUTKNN || method == AAKNN || method == NCUTKNNMULTI){
            char knnfile[100] = {0};
            strcat(knnfile,knnfiledir);
            strcat(knnfile,"/");
            strcat(knnfile,shortname);
            strcat(knnfile,".bin");
            //printf("knn file path:%s\n",knnfile);
            //read knn table
            Table2D<int> temp_knntable; 
            readbinfile(temp_knntable,knnfile, KNN_K/*8*/,imgw * imgh);
            knntable = Table2D<int>(imgw*imgh,KNN_K); // index from zero, KNN_K rows
            for(int i=0;i<KNN_K;i++){
                for(int j=0;j<imgw*imgh;j++){
                    knntable[j][i] = temp_knntable[i/*8*/][j]-1;
                }
            }
            temp_knntable.resize(1,1);
        }
            
        // run for each image
        clock_t start = clock();
	    Table2D<Label> solution;
	    Table2D<int> solution_multi;
	   
	    if(method == NCUTKNN){
            solution = ncutknnsegmentation(image, knntable, w_smooth, initlabeling, hardconstraints);
            knntable.resize(1,1);
	    }
	    else if(method == AAKNN){
            solution = aaknnsegmentation(image, knntable, w_smooth, initlabeling, hardconstraints);
            knntable.resize(1,1);
	    }
	    else if(method == NCUTKNNMULTI){
            solution_multi = ncutknnmultisegmentation(image, knntable, w_smooth, initlabeling_multi, numColor);
            knntable.resize(1,1);
	    }
	    clock_t finish = clock();
        double timeforoneimage = (double)(finish-start)/CLOCKS_PER_SEC;
        totaltime += timeforoneimage;
        
        // save output
	    if((outputdir!=NULL) &&( method != NCUTKNNMULTI))
	        //savebinarylabeling(image.img, solution,(outputdir+string("/")+string(shortname) +"_s"+pch::to_string(w_smooth)+string(".bmp")).c_str());
	        savebinarylabeling(image.img, solution,(outputdir+string("/")+string(shortname) +string("_")+string(methodstr)+"_s"+pch::to_string(w_smooth)+string(".bmp")).c_str());
	    
	    if((outputdir!=NULL) &&( method == NCUTKNNMULTI)){
	        savemultilabeling(solution_multi,(outputdir+string("/")+string(shortname) +string("_")+string(methodstr)+"_s"+pch::to_string(w_smooth)+string(".bmp")).c_str(), colors,image.img);
	    }
        // measures
        if(NOMEASURE == errormeasure){
            imgid++;
            free(shortname);
            continue;
        }
        // ground truth
        Table2D<Label> gt = getinitlabeling(loadImage<RGB>((dbdir+string("/groundtruth/")+string(shortname) + string(".bmp")).c_str()),255,0);
        if(ERRORRATE == errormeasure){
            if( userinput == BOX && hardconstraintsflag )
                measures[imgid] = geterrorcount(solution,gt)/ (double) countintable(hardconstraints,NONE);
            else
                measures[imgid] = geterrorcount(solution,gt)/ (double)(imgw*imgh);
        }
        else if(JACCARD == errormeasure)
            measures[imgid] = jaccard(solution, gt, OBJ);
        else if(FMEASURE == errormeasure)
            measures[imgid] = fmeasure(solution, gt, OBJ);
        printf("measure %.3f\n",measures[imgid]);
        imgid++;
        free(shortname);
    }
	printf("average measure %.3f\n", arrayMean(measures, numimg));
    
    //}
    cout<<"time for segmentation "<<totaltime<<" seconds!"<<endl;
    return -1;
}
