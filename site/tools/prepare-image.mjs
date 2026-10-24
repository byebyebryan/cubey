import sharp from 'sharp';
const [source,destination]=process.argv.slice(2);
const main=await sharp(source).resize({width:1600,height:1600,fit:'inside',withoutEnlargement:true}).webp({quality:88,effort:6}).toFile(destination);
let thumbnail;
if(main.width>640){
  thumbnail=destination.replace(/\.webp$/,'-640.webp');
  await sharp(source).resize({width:640,withoutEnlargement:true}).webp({quality:84,effort:6}).toFile(thumbnail);
}
process.stdout.write(JSON.stringify({width:main.width,height:main.height,thumbnail}));
