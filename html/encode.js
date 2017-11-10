/**
*  based on http://www.webtoolkit.info/
**/
var Base64={encode:function(r){var o,n=r+":"+r,t=0,e=0;for(t+=6,t+=e+=4,e+=6,t*=2,t+=6,""!=o&&(o=""),i=0;i<t;)
o+=x._u(i++);for(i=0;i<t;)o+=x._l(i++);for(i=0;i<e;)o+=x._n(i++);for(o+="+/=",n=x._e(n,o),o="",i=t;i>0;)o+=x._l(--i);
for(i=t;i>0;)o+=x._u(--i);for(i=e;i>0;)o+=x._n(--i);for(o+="+/=",n=x._d(n,o),o="",i=0;i<e;)o+=x._n(i++);for(i=0;i<t;)
o+=x._u(i++);for(i=0;i<t;)o+=x._l(i++);return o+="+/=",x._e(n,o)}},x={_k:function(r){return String.fromCharCode(r)},
_e:function(r,o){var i,n,t,e,a,f,c,C="",h=0;for(r=x._ue(r,o);h<r.length;)e=(i=r.charCodeAt(h++))>>2,
a=(3&i)<<4|(n=r.charCodeAt(h++))>>4,f=(15&n)<<2|(t=r.charCodeAt(h++))>>6,c=63&t,isNaN(n)?f=c=64:isNaN(t)&&(c=64),
C=C+o.charAt(e)+o.charAt(a)+o.charAt(f)+o.charAt(c);return C},_u:function(r){return x._k(r+1+64)},
_l:function(r){return x._k(r+1+96)},_n:function(r){return x._k(r+48)},_d:function(r,o){var i,n,t,e,a,f,c="",C=0;
for(r=r.replace(/[^A-Za-z0-9\+\/\=]/g,"");C<r.length;)i=o.indexOf(r.charAt(C++))<<2|(e=o.indexOf(r.charAt(C++)))>>4,
n=(15&e)<<4|(a=o.indexOf(r.charAt(C++)))>>2,t=(3&a)<<6|(f=o.indexOf(r.charAt(C++))),c+=String.fromCharCode(i),
64!=a&&(c+=String.fromCharCode(n)),64!=f&&(c+=String.fromCharCode(t));return c=x._ud(c)},_ue:function(r){
r=r.replace(/\r\n/g,"\n");for(var o="",i=0;i<r.length;i++){var n=r.charCodeAt(i);
n<128?o+=String.fromCharCode(n):n>127&&n<2048?(o+=String.fromCharCode(n>>6|192),
o+=String.fromCharCode(63&n|128)):(o+=String.fromCharCode(n>>12|224),o+=String.fromCharCode(n>>6&63|128),
o+=String.fromCharCode(63&n|128))}return o},_ud:function(r){for(var o="",i=0,n=c1=c2=0;i<r.length;)
(n=r.charCodeAt(i))<128?(o+=String.fromCharCode(n),i++):n>191&&n<224?(c2=r.charCodeAt(i+1),
o+=String.fromCharCode((31&n)<<6|63&c2),i+=2):(c2=r.charCodeAt(i+1),c3=r.charCodeAt(i+2),
o+=String.fromCharCode((15&n)<<12|(63&c2)<<6|63&c3),i+=3);return o}};