// Run against the unmodified canonical toolkit source, never against the native
// adapter. Numeric reference only: no game assets, renderer or devices.
using System;
using System.Collections.Generic;
using System.Globalization;
using Dbce.TripleScreen;
const double width=708.417,height=398.484;
double[][] cases={
    new double[]{660,660,70,70,0,0,0,0,0,0},
    new double[]{660,660,70,70,0,0,0,0,0,1500},
    new double[]{660,313,70,70,0,0,0,0,0,1500},
    new double[]{660,313,65,70,80,8,25,15,30,400},
    new double[]{660,1200,70,65,80,0,0,0,0,20000},
    new double[]{660,900,20,75,-30,12,-40,25,50,1500}
};
foreach(var c in cases){
    var panels=TripleRigBuilder.Build(new TripleRigDefinition(width,height,c[0],c[2],c[3],c[4],c[5],c[9]));
    var rendered=TripleRigBuilder.Build(new TripleRigDefinition(width,height,c[1],c[2],c[3],c[4],c[5]));
    var eye=new Vector3d(c[6],c[7],c[8]);
    for(int p=0;p<3;p++){
        var warp=CurvedPanelWarp.Create(panels[p],eye,c[9]).WithSourceOn(rendered[p]);
        foreach(double u in new[]{0d,.25,.5,.75,1d})foreach(double v in new[]{0d,.5,1d}){
            var numbers=new List<double>{width,height,c[0],c[1],c[2],c[3],c[4],c[5],c[6],c[7],c[8],c[9],p,u,v};
            var point=CurvedPanelWarp.PanelPoint(panels[p],c[9],u,v);
            foreach(var vector in new[]{point,warp.SourceSurface.LowerLeft,warp.SourceSurface.LowerRight,warp.SourceSurface.UpperLeft}){
                numbers.Add(vector.X);numbers.Add(vector.Y);numbers.Add(vector.Z);
            }
            warp.SourceUv(u,v,out var su,out var sv);numbers.Add(su);numbers.Add(sv);
            Console.WriteLine(string.Join("\t",numbers.ConvertAll(x=>x.ToString("R",CultureInfo.InvariantCulture))));
        }
    }
}
