
var TargetPage=null;

function OnInit()
{
	TranslatePage();

	TargetPage=GetQueryString("target");
	
	// Fallback if the C++ -> JS signal fails (e.g., WebKit issues on macOS).
	setTimeout("JumpToTarget()", 30 * 1000);
}

function HandleStudio( pVal )
{
	let strCmd=pVal['command'];
	
	if(strCmd=='userguide_profile_load_finish')
	{
		JumpToTarget();
	}
}

function JumpToTarget()
{
	var page = TargetPage || '1';
	var url = '../' + page + '/index.html';
	try {
		window.location.replace(url);
	} catch (e) {
		window.location.href = url;
	}
}