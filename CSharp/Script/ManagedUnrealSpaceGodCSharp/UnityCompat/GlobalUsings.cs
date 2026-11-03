// In Unity `Object` in a file that imports both System and UnityEngine is UnityEngine.Object only where the file says
// so (`using Object = UnityEngine.Object;`); Unity's compiler setup also resolved Bootstrap's unqualified Object that
// way. One global alias keeps that meaning everywhere in the port (US-T27).
global using Object = UnityEngine.Object;
